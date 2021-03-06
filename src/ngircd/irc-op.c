/*
 * ngIRCd -- The Next Generation IRC Daemon
 * Copyright (c)2001-2013 Alexander Barton (alex@barton.de) and Contributors.
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
 * Channel operator commands
 */

#include "imp.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "defines.h"
#include "conn.h"
#include "channel.h"
#include "irc-macros.h"
#include "irc-write.h"
#include "lists.h"
#include "log.h"
#include "messages.h"
#include "parse.h"

#include "exp.h"
#include "irc-op.h"

/* Local functions */

static bool
try_kick(CLIENT *peer, CLIENT* from, const char *nick, const char *channel,
	 const char *reason)
{
	CLIENT *target = Client_Search(nick);

	if (!target)
		return IRC_WriteStrClient(from, ERR_NOSUCHNICK_MSG, Client_ID(from), nick);

	Channel_Kick(peer, target, from, channel, reason);
	return true;
}

/* Global functions */

/**
 * Handler for the IRC command "KICK".
 *
 * @param Client The client from which this command has been received.
 * @param Req Request structure with prefix and all parameters.
 * @return CONNECTED or DISCONNECTED.
 */
GLOBAL bool
IRC_KICK(CLIENT *Client, REQUEST *Req)
{
	CLIENT *from;
	char *itemList = Req->argv[0];
	const char* currentNick, *currentChannel, *reason;
	unsigned int channelCount = 1;
	unsigned int nickCount = 1;

	assert( Client != NULL );
	assert( Req != NULL );

	_IRC_ARGC_BETWEEN_OR_RETURN_(Client, Req, 2, 3)
	_IRC_GET_SENDER_OR_RETURN_(from, Req, Client)

	while (*itemList) {
		if (*itemList == ',') {
			*itemList = '\0';
			channelCount++;
		}
		itemList++;
	}

	itemList = Req->argv[1];
	while (*itemList) {
		if (*itemList == ',') {
			*itemList = '\0';
			nickCount++;
		}
		itemList++;
	}

	reason = Req->argc == 3 ? Req->argv[2] : Client_ID(from);
	currentNick = Req->argv[1];
	currentChannel = Req->argv[0];
	if (channelCount == 1) {
		while (nickCount > 0) {
			if (!try_kick(Client, from, currentNick,
				      currentChannel, reason))
				return false;

			while (*currentNick)
				currentNick++;

			currentNick++;
			nickCount--;
		}
	} else if (channelCount == nickCount) {
		while (nickCount > 0) {
			if (!try_kick(Client, from, currentNick,
				      currentChannel, reason))
				return false;

			while (*currentNick)
				currentNick++;

			while (*currentChannel)
				currentChannel++;

			currentNick++;
			currentChannel++;
			nickCount--;
		}
	} else {
		return IRC_WriteStrClient(Client, ERR_NEEDMOREPARAMS_MSG,
					Client_ID(Client), Req->command);
	}
	return true;
} /* IRC_KICK */

/**
 * Handler for the IRC command "INVITE".
 *
 * @param Client The client from which this command has been received.
 * @param Req Request structure with prefix and all parameters.
 * @return CONNECTED or DISCONNECTED.
 */
GLOBAL bool
IRC_INVITE(CLIENT *Client, REQUEST *Req)
{
	CHANNEL *chan;
	CLIENT *target, *from;
	const char *colon_if_necessary;
	bool remember = false;

	assert( Client != NULL );
	assert( Req != NULL );

	_IRC_ARGC_EQ_OR_RETURN_(Client, Req, 2)
	_IRC_GET_SENDER_OR_RETURN_(from, Req, Client)

	/* Search user */
	target = Client_Search(Req->argv[0]);
	if (!target || (Client_Type(target) != CLIENT_USER))
		return IRC_WriteStrClient(from, ERR_NOSUCHNICK_MSG,
				Client_ID(Client), Req->argv[0]);

	chan = Channel_Search(Req->argv[1]);
	if (chan) {
		/* Channel exists. Is the user a valid member of the channel? */
		if (!Channel_IsMemberOf(chan, from))
			return IRC_WriteStrClient(from, ERR_NOTONCHANNEL_MSG, Client_ID(Client), Req->argv[1]);

		/* Is the channel "invite-disallow"? */
		if (strchr(Channel_Modes(chan), 'V'))
			return IRC_WriteStrClient(from, ERR_NOINVITE_MSG,
				Client_ID(from), Channel_Name(chan));

		/* Is the channel "invite-only"? */
		if (strchr(Channel_Modes(chan), 'i')) {
			/* Yes. The user must be channel owner/admin/operator/halfop! */
			if (!strchr(Channel_UserModes(chan, from), 'q') &&
			    !strchr(Channel_UserModes(chan, from), 'a') &&
			    !strchr(Channel_UserModes(chan, from), 'o') &&
			    !strchr(Channel_UserModes(chan, from), 'h'))
				return IRC_WriteStrClient(from, ERR_CHANOPRIVSNEEDED_MSG,
						Client_ID(from), Channel_Name(chan));
			remember = true;
		}

		/* Is the target user already member of the channel? */
		if (Channel_IsMemberOf(chan, target))
			return IRC_WriteStrClient(from, ERR_USERONCHANNEL_MSG,
					Client_ID(from), Req->argv[0], Req->argv[1]);

		/* If the target user is banned on that channel: remember invite */
		if (Lists_Check(Channel_GetListBans(chan), target))
			remember = true;

		if (remember) {
			/* We must remember this invite */
			if (!Channel_AddInvite(chan, Client_Mask(target), true))
				return CONNECTED;
		}
	}

	LogDebug("User \"%s\" invites \"%s\" to \"%s\" ...", Client_Mask(from),
		 Req->argv[0], Req->argv[1]);

	/*
	 * RFC 2812 says:
	 * 'There is no requirement that the channel [..] must exist or be a valid channel'
	 * The problem with this is that this allows the "channel" to contain spaces,
	 * in which case we must prefix its name with a colon to make it clear that
	 * it is only a single argument.
	 */
	colon_if_necessary = strchr(Req->argv[1], ' ') ? ":":"";
	/* Inform target client */
	IRC_WriteStrClientPrefix(target, from, "INVITE %s %s%s", Req->argv[0],
					colon_if_necessary, Req->argv[1]);

	if (Client_Conn(target) > NONE) {
		/* The target user is local, so we have to send the status code */
		if (!IRC_WriteStrClientPrefix(from, target, RPL_INVITING_MSG,
			Client_ID(from), Req->argv[0], colon_if_necessary, Req->argv[1]))
			return DISCONNECTED;

		if (strchr(Client_Modes(target), 'a') &&
			!IRC_WriteStrClient(from, RPL_AWAY_MSG, Client_ID(from),
					Client_ID(target), Client_Away(target)))
				return DISCONNECTED;
	}
	return CONNECTED;
} /* IRC_INVITE */

/* -eof- */
