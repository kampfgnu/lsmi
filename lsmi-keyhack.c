/* 
 * Copyright (C) 2007 Jonathan Moore Liles
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

/* -- lsmi-keyhack.c
 *
 * title	Linux pSeudo Midi Input
 * date		March, 2007
 * author	Jonathan Moore Liles
 *
 * --
 *
 * : Keyboard Hack
 *
 *   This driver is for a hacked AT / PS/2 keyboard functioning as a MIDI
 *   controller.
 *
 *   It is somewhat specific to my own hardware, but, since it relies a
 *   learning capability rather than a fixed keymap, it should be equally
 *   useful for others wishing to build their own fake MIDI keyboard. Of
 *   course, such a keyboard will not be velocity sensitive, but this project
 *   is a good way to salvage both an old QWERTY keyboard and a manual from a
 *   decrepit analog organ or cheap PCM noise-maker.
 *
 *   The driver supports up to 88 musical keys, three footswitches, and several
 *   additional buttons for control and data entry. It has the rather
 *   unfortunate side-effect of rendering the console useless, unless, of
 *   course you have another (USB) keyboard to type on. I tried to get the
 *   interface working with the kernel's parkbd driver (AT keyboard over
 *   parallel port), but could never get a key through--and on top of that, the
 *   maintainer won't return my emails.
 *
 * :: The Hardware
 * 		
 *   To work in a musical setting, your keyboard controller must come from an
 *   AT / PS/2 keyboard designed before the invention of 'blocking'.  Such a
 *   keyboard will have real, mechanical switches (no membrane) and one silicon
 *   diode for each switch--enabling any number and combination of simultaneous
 *   keypresses. More modern keyboards omit the diodes (to save a nickle) and
 *   implement 'blocking' in the microcontroller to hide the deed (without the
 *   diodes and without the blocking logic, phantom keypresses would result
 *   from certain combinations of keys).
 *
 *   Once you've located such a keyboard you'll have to remove the controller
 *   board and all the switching diodes; then wire the controller's columns and
 *   rows up to your musical keyboard, using a diode (properly oriented) for
 *   each switch. It doesn't matter which keys you wire where, as the driver
 *   learning process will figure it out for you. If, during the learning
 *   proceedure, you find a key behaves strangely, rewire it and avoid using
 *   that particular row+column combination again (as you've probably stumbled
 *   upon the dreaded pause/break key, which has a rather unmanagable
 *   scancode).  If you wish, you may also wire up three footswitches and the
 *   18 button control pad.
 *
 *
 *   My control pad looks like the following:
 * 
 * 
 * >                      (LEDs)
 * > EXIT       MODE    D1  D2  D3
 * >
 * > Octave
 * > <-     ->          7   8   9
 * >
 * >
 * > Channel            4   5   6
 * > <-     ->
 * >
 * > Patch              1   2   3
 * > <-     ->              
 * >                        0
 *
 *
 *   If I had it to build over again? I'd probably have added a row of
 *   fixed-channel, fixed-octave buttons above the keys for addressing
 *   Freewheeling loops. Implementing this in software is up to you.
 *
 * :: The Software
 *
 *   Don't forget that you can use aseqnet to send the realtime MIDI data 
 *   over the network to another machine! (that's what I do)
 *
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

#define elementsof(x) ( sizeof( (x) ) / sizeof( (x)[0] ) )
#define min(x,min) ( (x) < (min) ? (min) : (x) )
#define max(x,max) ( (x) > (max) ? (max) : (x) )
#define testbit(bit, array)    (array[bit/8] & (1<<(bit%8)))

#define CLIENT_NAME "Pseudo-MIDI Keyboard Hack"
#define VERSION "0.6"
#define DOWN 1
#define UP 0

enum prog_modes { PATCH, BANK, CHANNEL };
enum prog_modes prog_mode = PATCH;

#define NUM_PROG_MODES 3

char *mode_names[] = { "CHANNEL", "PATCH", "BANK" };

char defaultdatabase[] = ".keydb";
char *database = defaultdatabase;

int verbose = 0;
int prog_index = 0;
static char prog_buf[4];
int channel = 0;

int octave = 5;
int octave_min = 0;
int octave_max = 9;


int fd;

snd_seq_t *seq = NULL;
int port;
struct timeval timeout;

char *sub_name = NULL;					/* subscriber */

char defaultdevice[] = "/dev/input/event0";
char *device = defaultdevice;

enum control_keys {
	CKEY_EXIT = 1,
	CKEY_MODE,
	CKEY_OCTAVE_DOWN,
	CKEY_OCTAVE_UP,
	CKEY_CHANNEL_DOWN,
	CKEY_CHANNEL_UP,
	CKEY_PATCH_DOWN,
	CKEY_PATCH_UP,
	CKEY_NUMERIC
};

/* button mapping */
struct map_s {
	enum control_keys control;
	int ev_type;
	int number;							/* note or controller # */
};

struct map_s map[KEY_MAX];

#define CKEY_MIN CKEY_EXIT
#define CKEY_MAX CKEY_PATCH_UP

char *key_names[] = {
	"",
	"EXIT",
	"MODE",
	"OCTAVE DOWN",
	"OCTAVE UP",
	"CHANNEL DOWN",
	"CHANNEL UP",
	"PATCH DOWN",
	"PATCH UP",
	"NUMERIC",
};

int
open_database ( char *filename )
{
	int dbfd;

	if ( -1 == ( dbfd = open( filename, O_RDONLY ) ) )
		return -1;

	read( dbfd, map, sizeof( map ) );

	close( dbfd );

	return 0;
}

int
close_database ( char *filename )
{
	int dbfd;

	if ( -1 == ( dbfd = creat( filename, 0666 ) ) )
		return -1;

	write( dbfd, map, sizeof( map ) );

	close( dbfd );

	return 0;
}

/**
 * Prepare to die gracefully
 */
void
clean_up ( void )
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
die ( int x )
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
	fprintf( stderr, "Usage: lsmi-keyhack [options]\n"
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
	const struct option long_opts[] = {
		{"help", no_argument, NULL, 'h'},
		{"port", required_argument, NULL, 'p'},
		{"channel", required_argument, NULL, 'c'},
		{"device", required_argument, NULL, 'd'},
		{"keydata", required_argument, NULL, 'k'},
		{"verbose", no_argument, NULL, 'v'},
		{NULL, 0, NULL, 0}
	};

	int c;

	while ( ( c = getopt_long( argc, argv, short_opts, long_opts, NULL ) )
			!= -1 )
	{
		switch ( c )
		{
			case 'h':
				fprintf( stderr, "Help\n" );
				exit( 0 );
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
					fprintf( stderr,
							 "Channel number must be bewteen 1 and 16!\n" );
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

		if ( iev.type != EV_KEY || iev.value == 2 )
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
get_key ( void )
{
	int key;
	int state;

	/* Ignore UPs from previous keypresses */
	do
	{
		key = get_keypress( &state );
	}
	while ( state != DOWN );

	/* Ignore other DOWNs while waiting for our key's UP */
	while ( get_keypress( &state ) != key );

	return key;
}

/**
 * Prompt for learning given control key
 */
void
learn_key ( int control )
{
	int keyi;

	printf( "Press the key that shall be known as %s.\n",
			key_names[control] );

	keyi = get_key();

	map[keyi].control = control;
}


/**
 * Analyze in-memory key map to determine number of keys and Middle C offset.
 */
void
analyze_map ( int *keys, int *mc_offset )
{
	int i;

	*keys = 0;
	*mc_offset = 0;

	for ( i = 0; i < elementsof( map ); i++ )
	{
		if ( map[i].ev_type == SND_SEQ_EVENT_NOTE )
		{
			( *keys )++;
			if ( map[i].number < *mc_offset )
				*mc_offset = map[i].number;
		}
	}

	*mc_offset = 0 - *mc_offset;
}

/**
 * set LEDs to indicate program mode
 */
void
update_leds ( void )
{
	struct input_event iev;
	int i;

	for ( i = 0; i < 3; i++ )
	{
		iev.type = EV_LED;

		iev.code = i;

		if ( i == prog_mode )
			iev.value = 1;
		else
			iev.value = 0;

		write( fd, &iev, sizeof( iev ) );
	}
}

/** 
 * Initialize event and uinput keyboard interfaces */
void
init_keyboard ( void )
{
	uint8_t evt[EV_MAX / 8 + 1];

	/* get capabilities */
	ioctl( fd, EVIOCGBIT( 0, sizeof( evt ) ), evt );

	if ( !( testbit( EV_KEY, evt ) && testbit( EV_MSC, evt ) ) )
	{
		fprintf( stderr,
				 "'%s' doesn't seem to be a keyboard! look in /proc/bus/input/devices to find the name of your keyboard's event device\n",
				 device );
		exit( 1 );
	}

	/* exclusive access */
	if ( ioctl( fd, EVIOCGRAB, 1 ) )
	{
		perror( "EVIOCGRAB" );
		exit( 1 );
	}
}


/** 
 * Prompt for learning input. Build key database.
 */
void
learn_mode ( void )
{
	int keyi;
	int i, key_offset;
	int learn_firstkey = 0;
	int learn_note = 0;
	int learn_keys = 0;

	printf( "Press the key that shall henceforth be known as EXIT\n" );

	keyi = get_key();

	map[keyi].control = CKEY_EXIT;

	printf
		( "Press each piano key in succession, beginning with the left-most. When you run out of keys, press the first one again.\n" );

	for ( ;; )
	{
		keyi = get_key();

#if 0
		ioctl( fd, KDMKTONE, ( 60 << 16 ) + 0x637 - ( learn_note * 10 ) );
#endif

		printf( "%i ", learn_note );
		fflush( stdout );

		if ( keyi == learn_firstkey )
			break;
		else if ( !learn_firstkey )
			learn_firstkey = keyi;

		map[keyi].control = 0;
		map[keyi].ev_type = SND_SEQ_EVENT_NOTE;
		map[keyi].number = learn_note++;

		learn_keys++;
	}

	printf( "\n%i keys encoded.\nNow press the key that shall be middle C.\n",
			learn_keys );

	keyi = get_key();

	key_offset = map[keyi].number;

	for ( i = 0; i < elementsof( map ); i++ )
	{
		if ( map[i].ev_type == SND_SEQ_EVENT_NOTE )
			map[i].number -= key_offset;
	}

	if ( map[keyi].number + ( 12 * octave ) != 60 )
	{
		fprintf( stderr, "Error in key logic! ( middle C == %i )\n",
				 map[keyi].number + ( 12 * octave ) );
	}

	printf
		( "Basic configuration complete. Press EXIT if you'd like to stop learning now, or any other key if you'd like to continue and configure the auxilliary input methods.\n" );

	keyi = get_key();

	if ( map[keyi].control == CKEY_EXIT )
		return;

	printf
		( "If your device has 18 key control pad, and you would like to program it now, press any key. To skip this step (and move on to pedals/footswitches), press EXIT.\n" );

	keyi = get_key();

	if ( map[keyi].control != CKEY_EXIT )
	{
		printf( "Press buttons 0 through 9 in ascending numerical order.\n" );

		for ( i = 0; i < 10; i++ )
		{
			keyi = get_key();

			printf( "%i encoded. ", i );
			fflush( stdout );

			map[keyi].control = CKEY_NUMERIC;
			map[keyi].number = i;
		}

		for ( i = CKEY_MIN + 1; i <= CKEY_MAX; i++ )
			learn_key( i );
	}

	printf( "Press and release the Sustain Pedal.\n" );

	keyi = get_key();

	map[keyi].ev_type = SND_SEQ_EVENT_CONTROLLER;
	map[keyi].number = 64;

	printf( "Press and release the Portamento Pedal.\n" );

	keyi = get_key();

	map[keyi].ev_type = SND_SEQ_EVENT_CONTROLLER;
	map[keyi].number = 65;

	printf( "Press and release the Soft Pedal.\n" );

	keyi = get_key();

	map[keyi].ev_type = SND_SEQ_EVENT_CONTROLLER;
	map[keyi].number = 67;

	printf( "\nLearning Complete!\n" );
}


/** main 
 *
 */
int
main ( int argc, char **argv )
{
	int keys = 0;
	int mc_offset = 0;

	snd_seq_event_t ev;

	int patch = 0;
	int bank = 0;

	fprintf( stderr, "lsmi-keyhack" " v" VERSION "\n" );

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
		else if ( snd_seq_connect_to( seq, port, addr.client, addr.port ) <
				  0 )
		{
			fprintf( stderr, "Error creating subscription for port %i:%i",
					 addr.client, addr.port );
			exit( 1 );
		}
	}

	fprintf( stderr, "Initializing keyboard...\n" );

	if ( -1 == ( fd = open( device, O_RDWR ) ) )
	{
		fprintf( stderr, "Error opening event interface! (%s)\n",
				 strerror( errno ) );
		exit( 1 );
	}

	init_keyboard();

	set_traps();

	update_leds();

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
				 "Make sure your \"keyboard\" device is connected!\n" );

		learn_mode();
	}

	analyze_map( &keys, &mc_offset );

	octave_min = ( mc_offset / 12 ) + 1;
	octave_max = 9 - ( ( keys - mc_offset ) / 12 );

	fprintf( stderr,
			 "%i keys, middle C is %ith from the left, lowest MIDI octave == %i, highest, %i\n",
			 keys, mc_offset + 1, octave_min, octave_max );

	fprintf( stderr, "Waiting for events...\n" );

	for ( ;; )
	{
		int keyi, newstate;

		keyi = get_keypress( &newstate );

		snd_seq_ev_clear( &ev );

		if ( map[keyi].control )
		{
			snd_seq_event_t e;

			if ( newstate == UP )
				continue;

			switch ( map[keyi].control )
			{
					/* All notes off */
					snd_seq_ev_set_controller( &ev, channel, 123, 0 );
					send_event( &ev );
					snd_seq_ev_clear( &ev );

				case CKEY_EXIT:
					fprintf( stderr, "Exiting...\n" );

					if ( close_database( database ) < 0 )
						fprintf( stderr, "Error saving database!\n" );

					clean_up();

					exit( 0 );

				case CKEY_MODE:

					prog_mode =
						prog_mode + 1 >
						NUM_PROG_MODES - 1 ? 0 : prog_mode + 1;
					fprintf( stderr, "Input mode change to %s\n",
							 mode_names[prog_mode] );

					update_leds();

					break;

				case CKEY_OCTAVE_DOWN:
					octave = min( octave - 1, octave_min );
					break;
				case CKEY_OCTAVE_UP:
					octave = max( octave + 1, octave_max );
					break;
				case CKEY_CHANNEL_DOWN:
					channel = min( channel - 1, 0 );
					break;
				case CKEY_CHANNEL_UP:
					channel = max( channel + 1, 15 );
					break;
				case CKEY_PATCH_DOWN:
					if ( patch == 0 && bank > 0 )
					{
						bank = min( bank - 1, 0 );
						patch = 127;

						snd_seq_ev_set_controller( &e, channel, 0, bank );
						send_event( &e );
					}
					else
						patch = min( patch - 1, 0 );

					snd_seq_ev_set_pgmchange( &ev, channel, patch );
					break;
				case CKEY_PATCH_UP:
					if ( patch == 127 && bank < 127 )
					{
						bank = max( bank + 1, 127 );
						patch = 0;

						snd_seq_ev_set_controller( &e, channel, 0, bank );
						send_event( &e );
					}
					else
						patch = max( patch + 1, 127 );

					snd_seq_ev_set_pgmchange( &ev, channel, patch );
					break;

				case CKEY_NUMERIC:
				{
					struct timeval tv;

					gettimeofday( &tv, NULL );
					/* Timeout in 5 secs */

					if ( tv.tv_sec - timeout.tv_sec >= 5 )
					{
						prog_index = 0;
					}

					timeout = tv;

					if ( prog_index == 0 )
						printf( "INPUT %s #: ", mode_names[prog_mode] );
				}

					prog_buf[prog_index++] = 48 + map[keyi].number;
					printf( "%i", map[keyi].number );
					fflush( stdout );

					if ( prog_index == 2 && prog_mode == CHANNEL )
					{

						/* FIXME: all notes off->channel */

						prog_buf[++prog_index] = '\0';
						channel = atoi( prog_buf );

						channel = max( channel, 15 );

						prog_index = 0;

						printf( " ENTER\n" );
					}
					else if ( prog_index == 3 )
					{
						prog_buf[++prog_index] = '\0';

						switch ( prog_mode )
						{
							case PATCH:
								patch = atoi( prog_buf );

								patch = max( patch, 127 );

								snd_seq_ev_set_pgmchange( &ev, channel,
														  patch );

								break;
							case BANK:
								bank = atoi( prog_buf );

								bank = max( bank, 127 );

								snd_seq_ev_set_controller( &ev, channel, 0,
														   bank );
								break;
							default:
								fprintf( stderr, "Internal error!\n" );
						}

						prog_index = 0;
						printf( " ENTER\n" );
					}

					break;
				default:
					fprintf( stderr, "Internal error!\n" );
			}

			send_event( &ev );

			continue;
		}
		else
			switch ( map[keyi].ev_type )
			{
				case SND_SEQ_EVENT_CONTROLLER:

					snd_seq_ev_set_controller( &ev, channel,
											   map[keyi].number,
											   newstate == DOWN ? 127 : 0 );

					break;

				case SND_SEQ_EVENT_NOTE:

					if ( newstate == DOWN )
						snd_seq_ev_set_noteon( &ev, channel,
											   map[keyi].number +
											   ( 12 * octave ), 64 );
					else
						snd_seq_ev_set_noteoff( &ev, channel,
												map[keyi].number +
												( 12 * octave ), 64 );
					break;

				default:
					fprintf( stderr, "Key has invalid mapping!\n" );
					break;
			}

		send_event( &ev );
	}
}
