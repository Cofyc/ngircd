/*
 * ngIRCd -- The Next Generation IRC Daemon
 * Copyright (c)2001-2011 Alexander Barton (alex@barton.de) and Contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * Please read the file COPYING, README and AUTHORS for more information.
 */

#include "portab.h"

/**
 * @file
 * IRC operator commands
 */

#include "imp.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "ngircd.h"
#include "conn-func.h"
#include "conf.h"
#include "channel.h"
#include "class.h"
#include "irc-write.h"
#include "log.h"
#include "match.h"
#include "messages.h"
#include "parse.h"
#include "op.h"

#include <exp.h>
#include "irc-oper.h"

/**
 * Handle invalid received OPER command.
 * Log OPER attempt and send error message to client.
 */
static bool
Bad_OperPass(CLIENT *Client, char *errtoken, char *errmsg)
{
	Log(LOG_WARNING, "Got invalid OPER from \"%s\": \"%s\" -- %s",
	    Client_Mask(Client), errtoken, errmsg);
	IRC_SetPenalty(Client, 3);
	return IRC_WriteStrClient(Client, ERR_PASSWDMISMATCH_MSG,
				  Client_ID(Client));
} /* Bad_OperPass */

/**
 * Handler for the IRC "OPER" command.
 *
 * See RFC 2812, 3.1.4 "Oper message".
 *
 * @param Client The client from which this command has been received.
 * @param Req Request structure with prefix and all parameters.
 * @return CONNECTED or DISCONNECTED.
 */
GLOBAL bool
IRC_OPER( CLIENT *Client, REQUEST *Req )
{
	struct Conf_Oper *op;
	size_t len, i;

	assert( Client != NULL );
	assert( Req != NULL );

	if (Req->argc != 2)
		return IRC_WriteStrClient(Client, ERR_NEEDMOREPARAMS_MSG,
					  Client_ID(Client), Req->command);

	len = array_length(&Conf_Opers, sizeof(*op));
	op = array_start(&Conf_Opers);
	for (i = 0; i < len && strcmp(op[i].name, Req->argv[0]); i++)
		;
	if (i >= len)
		return Bad_OperPass(Client, Req->argv[0], "not configured");

	if (strcmp(op[i].pwd, Req->argv[1]) != 0)
		return Bad_OperPass(Client, op[i].name, "bad password");

	if (op[i].mask && (!Match(op[i].mask, Client_Mask(Client))))
		return Bad_OperPass(Client, op[i].mask, "hostmask check failed");

	if (!Client_HasMode(Client, 'o')) {
		Client_ModeAdd(Client, 'o');
		if (!IRC_WriteStrClient(Client, "MODE %s :+o",
					Client_ID(Client)))
			return DISCONNECTED;
		IRC_WriteStrServersPrefix(NULL, Client, "MODE %s :+o",
					  Client_ID(Client));
	}

	if (!Client_OperByMe(Client))
		Log(LOG_NOTICE|LOG_snotice,
		    "Got valid OPER from \"%s\", user is an IRC operator now.",
		    Client_Mask(Client));

	Client_SetOperByMe(Client, true);
	return IRC_WriteStrClient(Client, RPL_YOUREOPER_MSG, Client_ID(Client));
} /* IRC_OPER */

/**
 * Handler for the IRC "DIE" command.
 *
 * See RFC 2812, 4.3 "Die message".
 *
 * @param Client The client from which this command has been received.
 * @param Req Request structure with prefix and all parameters.
 * @return CONNECTED or DISCONNECTED.
 */
GLOBAL bool
IRC_DIE(CLIENT * Client, REQUEST * Req)
{
	/* Shut down server */

	CONN_ID c;
	CLIENT *cl;

	assert(Client != NULL);
	assert(Req != NULL);

	if (!Op_Check(Client, Req))
		return Op_NoPrivileges(Client, Req);

	/* Bad number of parameters? */
#ifdef STRICT_RFC
	if (Req->argc != 0)
#else
	if (Req->argc > 1)
#endif
		return IRC_WriteStrClient(Client, ERR_NEEDMOREPARAMS_MSG,
					  Client_ID(Client), Req->command);

	/* Is a message given? */
	if (Req->argc > 0) {
		c = Conn_First();
		while (c != NONE) {
			cl = Conn_GetClient(c);
			if (Client_Type(cl) == CLIENT_USER)
				IRC_WriteStrClient(cl, "NOTICE %s :%s",
						Client_ID(cl), Req->argv[0]);
			c = Conn_Next(c);
		}
	}

	Log(LOG_NOTICE | LOG_snotice, "Got DIE command from \"%s\" ...",
	    Client_Mask(Client));
	NGIRCd_SignalQuit = true;

	return CONNECTED;
} /* IRC_DIE */

/**
 * Handler for the IRC "REHASH" command.
 *
 * See RFC 2812, 4.2 "Rehash message".
 *
 * @param Client The client from which this command has been received.
 * @param Req Request structure with prefix and all parameters.
 * @return CONNECTED or DISCONNECTED.
 */
GLOBAL bool
IRC_REHASH( CLIENT *Client, REQUEST *Req )
{
	/* Reload configuration file */

	assert( Client != NULL );
	assert( Req != NULL );

	if (!Op_Check(Client, Req))
		return Op_NoPrivileges(Client, Req);

	/* Bad number of parameters? */
	if (Req->argc != 0)
		return IRC_WriteStrClient(Client, ERR_NEEDMOREPARAMS_MSG,
					  Client_ID(Client), Req->command );

	Log(LOG_NOTICE|LOG_snotice, "Got REHASH command from \"%s\" ...",
	    Client_Mask(Client));
	IRC_WriteStrClient(Client, RPL_REHASHING_MSG, Client_ID(Client));

	raise(SIGHUP);

	return CONNECTED;
} /* IRC_REHASH */

/**
 * Handler for the IRC "RESTART" command.
 *
 * See RFC 2812, 4.4 "Restart message".
 *
 * @param Client The client from which this command has been received.
 * @param Req Request structure with prefix and all parameters.
 * @return CONNECTED or DISCONNECTED.
 */
GLOBAL bool
IRC_RESTART( CLIENT *Client, REQUEST *Req )
{
	/* Restart IRC server (fork a new process) */

	assert( Client != NULL );
	assert( Req != NULL );

	if (!Op_Check(Client, Req))
		return Op_NoPrivileges(Client, Req);

	/* Bad number of parameters? */
	if (Req->argc != 0)
		return IRC_WriteStrClient(Client, ERR_NEEDMOREPARAMS_MSG,
					  Client_ID(Client), Req->command);

	Log(LOG_NOTICE|LOG_snotice, "Got RESTART command from \"%s\" ...",
	    Client_Mask(Client));
	NGIRCd_SignalRestart = true;

	return CONNECTED;
} /* IRC_RESTART */

/**
 * Handler for the IRC "CONNECT" command.
 *
 * See RFC 2812, 3.4.7 "Connect message".
 *
 * @param Client The client from which this command has been received.
 * @param Req Request structure with prefix and all parameters.
 * @return CONNECTED or DISCONNECTED.
 */
GLOBAL bool
IRC_CONNECT(CLIENT * Client, REQUEST * Req)
{
	CLIENT *from, *target;

	assert(Client != NULL);
	assert(Req != NULL);

	if (Client_Type(Client) != CLIENT_SERVER
	    && !Client_HasMode(Client, 'o'))
		return Op_NoPrivileges(Client, Req);

	/* Bad number of parameters? */
	if (Req->argc != 1 && Req->argc != 2 && Req->argc != 3 &&
	    Req->argc != 5 && Req->argc != 6)
		return IRC_WriteStrClient(Client, ERR_NEEDMOREPARAMS_MSG,
					  Client_ID(Client), Req->command);

	/* Invalid port number? */
	if ((Req->argc > 1) && atoi(Req->argv[1]) < 1)
		return IRC_WriteStrClient(Client, ERR_NEEDMOREPARAMS_MSG,
					  Client_ID(Client), Req->command);

	from = Client;
	target = Client_ThisServer();

	if (Req->argc == 3 || Req->argc == 6) {
		/* This CONNECT has a target parameter */
		if (Client_Type(Client) == CLIENT_SERVER && Req->prefix)
			from = Client_Search(Req->prefix);
		if (! from)
			return IRC_WriteStrClient(Client, ERR_NOSUCHNICK_MSG,
					Client_ID(Client), Req->prefix);

		target = (Req->argc == 3) ? Client_Search(Req->argv[2])
					  : Client_Search(Req->argv[5]);
		if (! target || Client_Type(target) != CLIENT_SERVER)
			return IRC_WriteStrClient(from, ERR_NOSUCHSERVER_MSG,
					Client_ID(from), Req->argv[0]);
	}

	if (target != Client_ThisServer()) {
		/* Forward CONNECT command ... */
		if (Req->argc == 3)
			IRC_WriteStrClientPrefix(target, from,
				 "CONNECT %s %s :%s", Req->argv[0],
				 Req->argv[1], Req->argv[2]);
		else
			IRC_WriteStrClientPrefix(target, from,
				"CONNECT %s %s %s %s %s :%s", Req->argv[0],
				Req->argv[1], Req->argv[2], Req->argv[3],
				Req->argv[4], Req->argv[5]);
		return CONNECTED;
	}

	if (!Op_Check(from, Req))
		return Op_NoPrivileges(Client, Req);

	switch (Req->argc) {
	case 1:
		if (!Conf_EnablePassiveServer(Req->argv[0]))
			return IRC_WriteStrClient(from, ERR_NOSUCHSERVER_MSG,
						  Client_ID(from),
						  Req->argv[0]);
		break;
	case 2:
	case 3:
		/* Connect configured server */
		if (!Conf_EnableServer
		    (Req->argv[0], (UINT16) atoi(Req->argv[1])))
			return IRC_WriteStrClient(from, ERR_NOSUCHSERVER_MSG,
						  Client_ID(from),
						  Req->argv[0]);
		break;
	default:
		/* Add server */
		if (!Conf_AddServer
		    (Req->argv[0], (UINT16) atoi(Req->argv[1]), Req->argv[2],
		     Req->argv[3], Req->argv[4]))
			return IRC_WriteStrClient(from, ERR_NOSUCHSERVER_MSG,
						  Client_ID(from),
						  Req->argv[0]);
	}

	Log(LOG_NOTICE | LOG_snotice,
	    "Got CONNECT command from \"%s\" for \"%s\".", Client_Mask(from),
	    Req->argv[0]);
	IRC_SendWallops(Client_ThisServer(), Client_ThisServer(),
			"Received CONNECT %s from %s",
			Req->argv[0], Client_ID(from));

	return CONNECTED;
} /* IRC_CONNECT */

/**
 * Handler for the IRC "DISCONNECT" command.
 *
 * This command is not specified in the IRC RFCs, it is an extension
 * of ngIRCd: it shuts down and disables a configured server connection.
 *
 * @param Client The client from which this command has been received.
 * @param Req Request structure with prefix and all parameters.
 * @return CONNECTED or DISCONNECTED.
 */
GLOBAL bool
IRC_DISCONNECT(CLIENT * Client, REQUEST * Req)
{
	CONN_ID my_conn;

	assert(Client != NULL);
	assert(Req != NULL);

	if (!Op_Check(Client, Req))
		return Op_NoPrivileges(Client, Req);

	/* Bad number of parameters? */
	if (Req->argc != 1)
		return IRC_WriteStrClient(Client, ERR_NEEDMOREPARAMS_MSG,
					  Client_ID(Client), Req->command);

	IRC_SendWallops(Client_ThisServer(), Client_ThisServer(),
			"Received DISCONNECT %s from %s",
			Req->argv[0], Client_ID(Client));

	Log(LOG_NOTICE | LOG_snotice,
	    "Got DISCONNECT command from \"%s\" for \"%s\".",
	    Client_Mask(Client), Req->argv[0]);

	/* Save ID of this connection */
	my_conn = Client_Conn(Client);

	/* Disconnect configured server */
	if (!Conf_DisableServer(Req->argv[0]))
		return IRC_WriteStrClient(Client, ERR_NOSUCHSERVER_MSG,
					  Client_ID(Client), Req->argv[0]);

	/* Are we still connected or were we killed, too? */
	if (Conn_GetClient(my_conn))
		return CONNECTED;
	else
		return DISCONNECTED;
} /* IRC_DISCONNECT */

/**
 * Handler for the IRC "WALLOPS" command.
 *
 * See RFC 2812, 4.7 "Operwall message".
 *
 * @param Client The client from which this command has been received.
 * @param Req Request structure with prefix and all parameters.
 * @return CONNECTED or DISCONNECTED.
 */
GLOBAL bool
IRC_WALLOPS( CLIENT *Client, REQUEST *Req )
{
	CLIENT *from;

	assert( Client != NULL );
	assert( Req != NULL );

	if (Req->argc != 1)
		return IRC_WriteStrClient(Client, ERR_NEEDMOREPARAMS_MSG,
					  Client_ID(Client), Req->command);

	switch (Client_Type(Client)) {
	case CLIENT_USER:
		if (!Client_OperByMe(Client))
			return IRC_WriteStrClient(Client, ERR_NOPRIVILEGES_MSG,
						  Client_ID(Client));
		from = Client;
		break;
	case CLIENT_SERVER:
		from = Client_Search(Req->prefix);
		break;
	default:
		return CONNECTED;
	}

	if (!from)
		return IRC_WriteStrClient(Client, ERR_NOSUCHNICK_MSG,
					  Client_ID(Client), Req->prefix);

	IRC_SendWallops(Client, from, "%s", Req->argv[0]);
	return CONNECTED;
} /* IRC_WALLOPS */

/**
 * Handle <?>LINE commands (GLINE, KLINE).
 *
 * @param Client The client from which this command has been received.
 * @param Req Request structure with prefix and all parameters.
 * @return CONNECTED or DISCONNECTED.
 */
GLOBAL bool
IRC_xLINE(CLIENT *Client, REQUEST *Req)
{
	CLIENT *from;
	int class;
	char class_c;

	assert(Client != NULL);
	assert(Req != NULL);

	from = Op_Check(Client, Req);
	if (!from)
		return Op_NoPrivileges(Client, Req);

	/* Bad number of parameters? */
	if (Req->argc != 1 && Req->argc != 3)
		return IRC_WriteStrClient(Client, ERR_NEEDMOREPARAMS_MSG,
					  Client_ID(Client), Req->command);

	switch(Req->command[0]) {
		case 'g':
		case 'G':
			class = CLASS_GLINE; class_c = 'G';
			break;
		case 'k':
		case 'K':
			class = CLASS_KLINE; class_c = 'K';
			break;
		default:
			Log(LOG_CRIT,
			    "IRC_xLINE() called for unknown line: %c!? Ignored.",
			    Req->command[0]);
			return CONNECTED;
	}

	if (Req->argc == 1) {
		/* Delete mask from list */
		Class_DeleteMask(class, Req->argv[0]);
		Log(LOG_NOTICE|LOG_snotice,
		    "\"%s\" deleted \"%s\" from %c-Line list.",
		    Client_Mask(from), Req->argv[0], class_c);
		if (class == CLASS_GLINE) {
			/* Inform other servers */
			IRC_WriteStrServersPrefix(Client, from, "%s %s",
						  Req->command, Req->argv[0]);

		}
	} else {
		/* Add new mask to list */
		if (Class_AddMask(class, Req->argv[0],
				  time(NULL) + atol(Req->argv[1]),
				  Req->argv[2])) {
			Log(LOG_NOTICE|LOG_snotice,
			    "\"%s\" added \"%s\" to %c-Line list: \"%s\" (%ld seconds).",
			    Client_Mask(from), Req->argv[0], class_c,
			    Req->argv[2], atol(Req->argv[1]));
			if (class == CLASS_GLINE) {
				/* Inform other servers */
				IRC_WriteStrServersPrefix(Client, from,
						"%s %s %s :%s", Req->command,
						Req->argv[0], Req->argv[1],
						Req->argv[2]);
			}
		}
	}

	return CONNECTED;
}

/* -eof- */
