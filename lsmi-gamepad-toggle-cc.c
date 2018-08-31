/* 
 * Copyright (C) 2006 Jonathan Moore Liles
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* lsmi-gamepad-toggle-cc.c
 *
 * Linux Pseudo MIDI Input -- Gamepad Hack
 * March, 2007
 *
 * This driver is for a USB gamepad/joypad functioning as a MIDI
 * controller to send CC messages from 13 to 13+[N buttons].
 *
 *
 * How does it work?
 * 
 * It tries to load the keymap file (~/.keydb).
 * If it does not exist, a little wizard asks you to configure you gamepad buttons.
 * Your buttons finally send CC messages in range 13 to 13+[N buttons]
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <alsa/asoundlib.h>

#include <sys/ioctl.h>

#include <sys/time.h>
#include <signal.h>
#include <getopt.h>

#include <linux/input.h>
#include <stdint.h>

#include "seq.h"
#include "sig.h"

#define testbit(bit, array)    (array[bit/8] & (1<<(bit%8)))

#define CLIENT_NAME "USB-Gamepad CC Toggler"
#define VERSION "0.1"
#define DOWN 1
#define UP 0

char defaultdatabase[] = ".keydb";
char *database = defaultdatabase;

int verbose = 0;
int channel = 0;

int fd;

snd_seq_t *seq = NULL;
int port;
struct timeval timeout;

char *sub_name = NULL;								/* subscriber */

char defaultdevice[] = "/dev/input/event0";
char *device = defaultdevice;

enum control_keys
{
	CKEY_EXIT = 1,
	CKEY_NUMERIC
};

typedef int bool;
#define true 1
#define false 0

/* button mapping */
struct map_s {
	enum control_keys control;
	int ev_type;
	int number;
	bool active;
};

struct map_s map[KEY_MAX];

int
open_database ( char *filename )
{
	int dbfd;

	if ( -1 == ( dbfd = open( filename, O_RDONLY ) ) )
		return -1;
	
	read( dbfd, map, sizeof ( map ) );

	close( dbfd );

	return 0;
}

int
close_database ( char *filename )
{
	int dbfd;

	if ( -1 == ( dbfd = creat( filename, 0666 ) ) )	
		return -1;
	
	write( dbfd, map, sizeof ( map ) );

	close( dbfd );

	return 0;
}

/**
 * Prepare to die gracefully
 */
void
clean_up( void )
{
	/* release the keyboard */
	ioctl( fd, EVIOCGRAB, 0 );

	close( fd );

	snd_seq_close( seq );
}

/** 
 * Signal handler
 */
void
die( int x )
{
  printf( "caught signal %d, cleaning up...\n", x );
  clean_up();
  exit( 1 );
}

/** 
 * print help
 */
void
usage ( void )
{
	fprintf( stderr, "Usage: lsmi-gamepad-toggle-cc [options]\n"
	"Options:\n\n"
		" -h | --help                   Show this message\n"
		" -d | --device specialfile     Event device to use (instead of event0)\n"
		" -v | --verbose                Be verbose (show note events)\n"
		" -c | --channel n              Initial MIDI channel\n"
		" -p | --port client:port       Connect to ALSA Sequencer client on startup\n"					
		" -k | --keydata file			Name file to read/write key mappings (instead of ~/.keydb)\n"
	"\n" );
}

/** 
 * process commandline arguments
 */
void
get_args ( int argc, char **argv )
{
	const char *short_opts = "hp:c:d:k:v";
	const struct option long_opts[] =
	{
		{ "help", no_argument, NULL, 'h' },
		{ "port", required_argument, NULL, 'p' },
		{ "channel", required_argument, NULL, 'c' },
		{ "device", required_argument, NULL, 'd' },
		{ "keydata", required_argument, NULL, 'k' },
		{ "verbose", no_argument, NULL, 'v' },
		{ NULL, 0, NULL, 0 }
	};

	int c;

	while ( ( c = getopt_long( argc, argv, short_opts, long_opts, NULL ))
			!= -1 )
	{
		switch (c)
		{
			case 'h':
				fprintf( stderr, "Help\n" );
				exit(0);
				break;
			case 'p':
				sub_name = optarg;
				break;
			case 'c':
				channel = atoi( optarg );

				fprintf( stderr, "Using initial channel %i", channel );

				if ( channel >= 1 && channel <= 16 )
					channel = channel - 1;
				else
				{
					fprintf( stderr, "Channel number must be between 1 and 16!\n" );
					exit( 1 );
				}
				break;
			case 'd':
				device = optarg;
				break;
			case 'k':
				database = optarg;
			case 'v':
				verbose = 1;
				break;
		}

	}
}

/** 
 * Block until keypress (down or up) is ready. Return raw key
 */
int
get_keypress ( int *state )
{
	struct input_event iev;
	int keyi;
	
	for ( ;; )
	{
		read( fd, &iev, sizeof( iev ) );

		if ( iev.type != EV_KEY ||
			 iev.value == 2 )
			continue;
	
		*state = iev.value == 0 ? UP : DOWN;
		keyi = iev.code;

		return keyi;
	}
}

/**
 * Get complete key (press and release), ignoring other releases. Return key
 * index
 */
int
get_key( void )
{
	int key;
	int state;

	/* Ignore UPs from previous keypresses */
	do {
		key = get_keypress( &state );
	} while ( state != DOWN );

	/* Ignore other DOWNs while waiting for our key's UP */
	while ( get_keypress( &state ) != key );

	return key;
}

/** 
 * Initialize event and uinput keyboard interfaces */
void
init_keyboard ( void )
{
  	uint8_t evt[EV_MAX / 8 + 1];

	/* get capabilities */
	ioctl( fd, EVIOCGBIT( 0, sizeof(evt)), evt );

	if ( ! ( testbit( EV_KEY, evt ) &&
			 testbit( EV_MSC, evt ) ) )
	{
		fprintf( stderr, "'%s' doesn't seem to be a keyboard! look in /proc/bus/input/devices to find the name of your keyboard's event device\n", device );
		exit( 1 );
	}

	/* exclusive access */
	if ( ioctl( fd, EVIOCGRAB, 1 ) )
	{
		perror( "EVIOCGRAB" );
		exit(1);
	}
}


/** 
 * Prompt for learning input. Build key database.
 */
void
learn_mode ( void )
{
	int keyi;
	int learn_firstkey = 0;
	int learn_note = 0;
	int learn_keys = 0;

	printf( "Press the key that shall henceforth be known as EXIT\n" );
	keyi = get_key();
	map[keyi].control = CKEY_EXIT;

	printf( "Press each button in succession, beginning with the left-most. When you run out of buttons, or do not want to assign all buttons, press the first one again.\n" );
	for ( ;; )
	{
		keyi = get_key();
		printf( "CC message number to send: %i, USB button key: %i ", 13 + learn_note, keyi );
		fflush(stdout);

		if ( keyi == learn_firstkey )
			break;
		else
		if ( ! learn_firstkey )
			learn_firstkey = keyi;
		
		map[keyi].control = 0;
		map[keyi].ev_type = SND_SEQ_EVENT_CONTROLLER;
		map[keyi].number  = 13 + learn_note++;
		map[keyi].active = false;

		learn_keys++;
	}

	printf( "\nLearning Complete!\n" );
}


/** main 
 *
 */
int
main ( int argc, char **argv )
{	
	snd_seq_event_t ev;

	fprintf( stderr, "lsmi-gamepad-toggle-cc" " v" VERSION "\n" );

	get_args( argc, argv );

	fprintf( stderr, "Registering MIDI port...\n" );

	seq = open_client( CLIENT_NAME );

	if ( NULL == seq )
	{
		fprintf( stderr, "Error opening alsa sequencer!\n" );
		exit( 1 );
	}

	if ( ( port = open_output_port( seq ) ) < 0 )
	{
		fprintf( stderr, "Error opening MIDI output port!\n" );
		exit( 1 );
	}

	if ( sub_name )
	{
		snd_seq_addr_t addr;

		if ( snd_seq_parse_address( seq, &addr, sub_name ) < 0 )
			fprintf( stderr, "Couldn't parse address '%s'", sub_name );
		else
		if ( snd_seq_connect_to( seq, port, addr.client, addr.port ) < 0 )
		{
			fprintf( stderr, "Error creating subscription for port %i:%i", addr.client, addr.port );
			exit( 1 );
		}
	}

	fprintf( stderr, "Initializing keyboard...\n" );
	if ( -1 == ( fd = open( device, O_RDWR ) ) )
	{
		fprintf( stderr, "Error opening event interface! (%s)\n", strerror( errno ) );
		exit(1);
	}
	init_keyboard();

	set_traps();

	fprintf( stderr, "Opening database...\n" );
	if ( database == defaultdatabase )
	{
		char *home = getenv( "HOME" );
		database = malloc( strlen( home ) + strlen( defaultdatabase ) + 2 );
		sprintf( database, "%s/%s", home, defaultdatabase );
	}

	if ( -1 == open_database( database ) )
	{
		fprintf( stderr, "******Key database missing or invalid******\n"
						 "Entering learning mode...\n"
						 "Make sure your device is connected!\n" );
		learn_mode();
	}

	fprintf( stderr, "Waiting for events...\n" );

	for ( ;; )
	{	
		int keyi, newstate;

		keyi = get_keypress( &newstate );
		
		snd_seq_ev_clear( &ev );

		if ( map[keyi].control ) {
			if ( newstate == UP )
				continue;

			snd_seq_ev_set_controller( &ev, channel, 123, 0 );
			send_event( &ev );
			snd_seq_ev_clear( &ev );

			fprintf( stderr, "Exiting...\n" );

			if ( close_database( database ) < 0 )
				fprintf( stderr, "Error saving database!\n" );

			clean_up();

			exit(0);
		}
		else {
			switch ( map[keyi].ev_type )
			{
				case SND_SEQ_EVENT_CONTROLLER:
					if (newstate == DOWN) {
						snd_seq_ev_set_controller( &ev, channel,
												    map[keyi].number,
												    !map[keyi].active ? 127 : 0 );
						map[keyi].active = !map[keyi].active;
					}

					break;

				default:
					fprintf( stderr,
							 "Key has invalid mapping!\n" );
					break;
			}
		}

		send_event( &ev );
	}
}

