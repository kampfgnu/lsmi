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

/* monterey.c
 * 
 * March, 2007
 *
 * Linux Pseudo-MIDI Input -- Monterey is a userspace driver for Monterey
 * International MK-9500 / K617W reversible keyboard.
 *
 * This device consists of a 104 QWERTY AT computer keyboard on one side and a
 * 37 key, velocity sensitive musical keyboard on the other. In addition to
 * flipping the unit over, one must flip a switch on the right side in order to
 * change the mode.
 *
 * The keyboard interface is standard, except that the musical side sends
 * two-scancode packets for each piano key press and release ('make' codes
 * only). The first scancode indicates the note, the second the velocity: 7
 * being the lowest, 1 the highest, and 0 representing a release (or sometimes
 * a very very light keypress). The musical side also has buttons for keys F1
 * through F9, left and right arrow keys, and return--all generating 'make'
 * codes only with no way to register release.
 *
 * I have absolutely no idea how the keyboard might have operated under
 * W**dows, and no real interest in finding out. 
 *
 * This driver creates an ALSA Sequencer port and attempts to fill it with
 * realtime MIDI data representing input from the musical side of the keyboard,
 * while passing regular textual data through the uinput interface and on to
 * Linux console or X Window System. There is no need to load a special
 * application or even run X in order to generate MIDI events: simply flip the
 * keyboard over and go nuts. The driver doesn't interfere at all with
 * multiple/international layouts (I use eng/ru). You can even use it along
 * side another (merged input) keyboard (ie. plugged into a laptop) and the
 * driver should be able to sort everything out (provided that you refrain from
 * typing on both keyboards simultaneously).
 *
 * From the nature of the keyboard's protocol, I doubt that it was ever
 * intended to be used this way and a bit of guess-work is required to separate
 * the musical and textual keypresses.
 *
 * If anyone has a (PS/2?) Creative Prodikeys keyboard they'd like to donate,
 * I'd be happy to add support for it if possible (assuming the mechanism is
 * similar).
 *
 *
 *  Function keys:
 *
 *   There's no reliable way to distinguish the function keys on the musical
 *   side from those on the QWERTY side in order to map them to channel,
 *   program change and so on. My solution is to interpret any function key
 *   (including arrows and return) pressed within two seconds of the 'quaver'
 *   key (F9) as a MIDI event. 
 *
 * 	 Program Change:
 *
 *    The first four keys (I-IV) function as patch pages, each page able to
 *    address 32 patches. To change to program number 2 (GM Bright Acoustic
 *    Piano), first press QUAVER, then function key I, then press the second
 *    piano key from the left (the first black key).
 *
 *   Bank Change:
 *
 *    Keys V-VIII work similarly to program change, but alter current bank
 *    instead. Note that you won't see any effect until you change patches as
 *    well.
 *
 *   Channel Change and Octave Change:
 *
 *    The arrow keys are used to change channel or octave. To lower or raise
 *    the octave (from that of middle C) the octave, press QUAVER followed by
 *    the appropriate arrow key. QUAVER may be ommitted between subsequent
 *    arrow presses, if they occur within 2 seconds of each other. To change
 *    the channel, press QUAVER followed by 'R' (return), then an arrow key.
 *
 * 	All of these heuristics are timing critical and might fail to operate under
 * 	heavy system loads. To ensure proper performance, use a high realtime
 * 	priority, like 99 (and it wouldn't hurt to do the same for your keyboard
 * 	controller's IRQ).
 *
 * QUIRKS:
 * 
 *  Events:
 *
 *   For some reason the kernel event layer drops KEY events, mostly when
 *   switching between a piano key and its associated text key. I believe this
 *   is a due to a bug in the repeat state tracking code, exposed here because
 *   the keyboard generates only 'make' scancodes on the musical side.  The
 *   driver works around this by tracking the MSC_SCAN events instead, but it's
 *   kind of a hack and requires massaging the events more than I'm comfortable
 *   with (might not work with PS2->USB adaptors, etc.)
 *
 *  Repeat Rate:
 *
 * 	 To prevent frustrating "stuck" repeats in X (the console doesn't appear to
 * 	 suffer from this problem) the driver converts all REPEAT events it passes
 * 	 to PRESSes.
 *
 * 	LEDs:
 *
 * 	 The LEDs don't work. This little driver is the only example of a real
 * 	 uinput filter I've seen.  I'm not sure the kernel developers antcipated
 * 	 the problem of managing the LEDs. Ideally it would be transparent. As it
 * 	 is, it would probably take a large amount of code to get the keyboard LEDs
 * 	 working again--which seems silly. Anyway, don't blame me; blame all the
 * 	 people who bitched and moaned for the ability to indicate their wives'
 * 	 fertility on their numlock LEDs. I never touch NUMLOCK, SCROLLLOCK, or the
 * 	 dreaded CAPSLOCK ANYWAY, SO THIS LITTLE SHORTCOMING DOESN'T BOTHER ME.
 * 
 *
 * PREREQUISITES:
 *
 * 	2.6 series kernel with evdev and uinput modules loaded.
 * 	ALSA Sequencer drivers and library.
 *
 * 	An MK-9500 or K617W keyboard...
 *
 */

#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <alsa/asoundlib.h>

#include <sys/ioctl.h>

#include <sys/time.h>
#include <getopt.h>

#include <linux/input.h>
#include <linux/uinput.h>

#include <sched.h>

#include <stdint.h>

#include "seq.h"
#include "sig.h"

#define elementsof(x) ( sizeof( (x) ) / sizeof( (x)[0] ) )
#define min(x,min) ( (x) < (min) ? (min) : (x) )
#define max(x,max) ( (x) > (max) ? (max) : (x) )

#define testbit(bit, array)    (array[bit/8] & (1<<(bit%8)))


#define STRIP_REPEATS 1


#define CLIENT_NAME "Pseudo-MIDI Keyboard"
#define VERSION "0.1"
#define DEVICE_NAME "Monterey Intl. MK-9500/K617W reversible keyboard"

#define FUNCTION_TIMEOUT 2							/* in seconds */


/* I get inter-byte delays of about 2500uS on all the hardware I tested (100Mhz
 * laptop to 1 and 2Ghz servers). Unfortunately, every 100th velocity byte or
 * so will have a delay of 10000uS (the kernel's fault?). Increase the timeout
 * if you experience dropped or stuck notes. The only disadvantage of longer
 * timeouts is that it's easier to trick the driver into generating notes by
 * pressing letter and number keys simultaneously (which, by the way, is nigh
 * impossible to do while typing naturally) */

#define KEY_TIMEOUT 15000							/* in microseconds */

/* global options */
int verbose = 0;
int no_velocity = 0;
int daemonize = 0;

/* MIDI state */
int channel = 0;
int patch_page = 0;
int bank_page = 0;
int patch = 0;
int bank = 0;
int octave = 5; 

const int octave_min = 3;
const int octave_max = 7;

char defaultdevice[] = "/dev/input/event0";
char *device = defaultdevice;

int fd;												/* keyboard fd */
int uifd;											/* uinput fd */

snd_seq_t *seq = NULL;								/* alsa_seq handle */
int port;											/* our output port */

char *sub_name = NULL;								/* subscriber */

static int keymap[KEY_MIN_INTERESTING + 1];
static int nummap[KEY_MINUS + 1];

/* valid key designators, in order */
const int keylist[] = {
	  KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
	  KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
	  KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z, KEY_8, KEY_9, KEY_MINUS,
	  KEY_EQUAL, KEY_BACKSLASH, KEY_LEFTBRACE, KEY_RIGHTBRACE, KEY_SEMICOLON,
	  KEY_APOSTROPHE, KEY_COMMA, KEY_DOT,
};

/* valid velocity values, in order */
const int numlist[] =
{
  KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7,
};

char notemap[38] = "-------------------------------------";

/** 
 * Initialize key and velocity key mappings */
void
init_maps ( void )
{
	int i;

	/* init maps to -1 */
	for ( i = 0; i < elementsof( keymap ); i++ )
		keymap[ i ] = -1;
	for ( i = 0; i < elementsof( nummap ); i++ )
		nummap[ i ] = -1;

	/* fill in valid keys */
	for ( i = 0; i < elementsof( keylist ); i++ )
		keymap[ keylist[i] ] = i;

	for ( i = 0; i < elementsof( numlist ); i++ )
		nummap[ numlist[i] ] = i;

}

/** 
 * Is /x/ a valid velocity byte?
 */
int
isnum( int x )
{
	if ( x >= elementsof( nummap ) )
		return 0;

	return nummap[ x ] >= 0;
}

/** 
 * Is /x/ a valid key byte?
 */
int
iskey( int x )
{
	if ( x >= elementsof( keymap ) )
		return 0;

	return keymap[ x ] >= 0;
}



/** 
 * Get ready to die gracefully.
 */
void
clean_up ( void )
{
	/* release the keyboard */
	ioctl( fd, EVIOCGRAB, 0 );

	/* unregister with uinput */
	ioctl( uifd, UI_DEV_DESTROY, 0 );

	close( uifd );
 	close( fd );

	snd_seq_close( seq );
}

/** 
 * Signal handler
 */
void
die ( int sig )
{
	printf( "caught signal %d, cleaning up...\n", sig );
	clean_up();
	exit( 1 );
}


/** usage
 *
 * print help
 *
 */
void
usage ( void )
{
	fprintf( stderr, "Usage: lsmi-monterey [options]\n"
	"Options:\n\n"
		" -h | --help                   Show this message\n"
		" -d | --device specialfile     Event device to use (instead of event0)\n"
		" -v | --verbose                Be verbose (show note events)\n"
		" -R | --realtime rtprio        Use realtime priority 'rtprio' (requires privs)\n"
		" -n | --no-velocity            Ignore velocity information from keyboard\n"
		" -c | --channel n              Initial MIDI channel\n"
		" -p | --port client:port       Connect to ALSA Sequencer client on startup\n" );
	fprintf( stderr, 
		" -z | --daemon                 Fork and don't print anything to stdout\n"
	"\n" );
}


/** 
 * process commandline arguments
 */
void
get_args ( int argc, char **argv )
{
	const char *short_opts = "hp:c:vnd:R:z";
	const struct option long_opts[] =
	{
		{ "help", no_argument, NULL, 'h' },
		{ "port", required_argument, NULL, 'p' },
		{ "channel", required_argument, NULL, 'c' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "no-veloticy", no_argument, NULL, 'n' },
		{ "device", required_argument, NULL, 'd' },
		{ "realtime", required_argument, NULL, 'R' },
		{ "daemon", no_argument, NULL, 'z' },
		{ NULL, 0, NULL, 0 }
	};

	int c;

	while ( ( c = getopt_long( argc, argv, short_opts, long_opts, NULL ))
			!= -1 )
	{
		switch (c)
		{
			case 'h':
				usage();
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
					fprintf( stderr, "Channel number must be bewteen 1 and 16!\n" );
					exit( 1 );
				}

				break;
			case 'v':
				verbose = 1;
				break;
			case 'd':
				device = optarg;
				break;
			case 'n':
				no_velocity = 1;
				break;
			case 'R':
				{
					struct sched_param sp;

					sp.sched_priority = atoi( optarg );

					if ( sched_setscheduler( 0, SCHED_FIFO, &sp ) < 0 )
					{
						perror( "sched_setscheduler()" );
						fprintf( stderr, "Failed to get realtime priority!\n" );
						exit( 1 );
					}

					fprintf( stderr, "Using realtime priority %i.\n", 
						sp.sched_priority );
				}
				break;
			case 'z':
				daemonize = 1;
				break;
		}
	}
}


enum prog_modes { MUSIC, PATCH, BANK, CHANNEL };
enum prog_modes prog_mode = MUSIC;

/** 
 * Process function key press, returns 0 if /key/ isn't a function key
 */
int
func_key ( int key )
{
	switch ( key )
	{
		case KEY_F1:
			patch_page = 0;
			prog_mode = PATCH;
			break;	
		case KEY_F2:
			patch_page = 1;
			prog_mode = PATCH;
			break;	
		case KEY_F3:
			patch_page = 2;
			prog_mode = PATCH;
			break;	
		case KEY_F4:
		 	patch_page = 3;
			prog_mode = PATCH;
			break;	

		case KEY_F5:
			bank_page = 0;
			prog_mode = BANK;
			break;	
		case KEY_F6:
			bank_page = 1;
			prog_mode = BANK;
			break;	
		case KEY_F7:
			bank_page = 2;
			prog_mode = BANK;
			break;	
		case KEY_F8:
		 	bank_page = 3;
			prog_mode = BANK;
			break;	

		case KEY_KP4:
			
			if ( prog_mode == CHANNEL )
			{
				channel = min( channel - 1, 0 );
				printf( "Channel Change: %i\n", channel );
			}
			else
			{
				octave = min( octave - 1, octave_min );
				printf("Octave Change: %i\n", octave );
			}


			break;
		case KEY_KP6:

			if ( prog_mode == CHANNEL )
			{
				channel = max( channel + 1, 15 );
				printf("Channel Change: %i\n", channel );
			}
			else
			{
				octave = max( octave + 1, octave_max );
				printf( "Octave Change: %i\n", octave );
			}

			break;

		case KEY_ENTER:
			prog_mode = CHANNEL;
			break;

		default:
			return 0;
	}

	return 1;

}


/** 
 * Pass input event pointed to by /ev/ to uinput
 */
void
send_key( struct input_event *ev )
{
#ifndef STRIP_REPEATS
	static int prev_key;
#endif

	struct input_event sc;

	sc.type = EV_MSC;
	sc.code = MSC_SCAN;
	sc.value = ev->code;
	sc.time = ev->time;

	write( uifd, &sc, sizeof ( sc ) );

#ifndef STRIP_REPEATS
	if ( ev->value != 0 )
	{
		ev->value = ev->code == prev_key ? 2 : 1;
		prev_key = ev->code;
	}
	else
		prev_key = 0;
#else
	/* X is broken for repeats, eat fudge */
	ev->value = ev->value == 2 ? 1 : ev->value;
#endif
		
	write( uifd, ev, sizeof ( sc ) );

	sc.type = EV_SYN;
	sc.code = SYN_REPORT;
	sc.value = 0;

	write( uifd, &sc, sizeof ( sc ) );
}

/** 
 * Initialize event and uinput keyboard interfaces
 */
void
init_keyboard ( void )
{
	struct uinput_user_dev uidev;
  	uint8_t evt[EV_MAX / 8 + 1];
  	uint8_t keys[KEY_MAX / 8 + 1];
	int i;

	/* get capabilities */
	ioctl( fd, EVIOCGBIT( 0, sizeof(evt)), evt );

	if ( ! ( testbit( EV_KEY, evt ) &&
			 testbit( EV_MSC, evt ) ) )
	{
		fprintf( stderr, "'%s' doesn't seem to be a keyboard! look in /proc/bus/input/devices to find the name of your keyboard's event device\n", device );
		exit( 1 );
	}

	/* get keys */
	ioctl( fd, EVIOCGBIT( EV_KEY, sizeof(keys)), keys );

	/* exclusive access */
	if ( ioctl( fd, EVIOCGRAB, 1 ) )
	{
		perror( "EVIOCGRAB" );
		exit(1);
	}

	if ( -1 == ( uifd = open( "/dev/input/uinput", O_RDWR | O_NDELAY ) ) )
	{
		fprintf( stderr, "Error opening uinput interface! (is the uinput module loaded?)\n" );
		exit(1);
	}

	memset( &uidev, 0, sizeof( uidev ) );

	strcpy( uidev.name, DEVICE_NAME );

	ioctl( uifd, UI_SET_EVBIT, EV_KEY );
	ioctl( uifd, UI_SET_EVBIT, EV_MSC );
    ioctl( uifd, UI_SET_EVBIT, EV_LED );
    ioctl( uifd, UI_SET_EVBIT, EV_REP );


    ioctl( uifd, UI_SET_LEDBIT, LED_NUML );
    ioctl( uifd, UI_SET_LEDBIT, LED_CAPSL );
    ioctl( uifd, UI_SET_LEDBIT, LED_SCROLLL );

	/* Copy keys capabilities */
	for ( i = 1; i < KEY_MAX; i++ )
		if ( testbit( i, keys ) )
			ioctl( uifd, UI_SET_KEYBIT, i );

	write( uifd, &uidev, sizeof( uidev ) );

	ioctl( uifd, UI_DEV_CREATE, 0 );
}

#if 0
double
usec_diff ( struct timeval *tv1, struct timeval *tv2 )
{
	double d = ( tv2->tv_sec + tv2->tv_usec * 1e-6 ) -
		   ( tv1->tv_sec + tv1->tv_usec * 1e-6 );

	d *= 1e6;

	fprintf( stderr, "key_delta = %iuS\n", (int)d );

	return d;

}
#endif

/** main 
 *
 */
int
main ( int argc, char **argv )
{
	static snd_seq_event_t ev;

	struct input_event iev;
	struct input_event prev_iev;

	#define KEY 0
	#define VELOCITY 1

	int expecting = KEY;
	int key, value, scancode = -1;

	time_t quaver_sec = 0;

	fprintf( stderr, "\nlsmi-monterey" " v" VERSION "\n" );

	get_args( argc, argv );

	init_maps();

	fprintf( stderr, "Registering MIDI port...\n" );

	if ( ( seq = open_client( CLIENT_NAME ) ) == NULL )
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

	if ( daemonize )
	{
		printf( "Running as daemon...\n" );
		if ( fork() )
			exit( 0 );
		else
		{
			fclose( stdout );
			fclose( stderr );
		}
	}

	set_traps();

	fprintf( stderr, "Waiting for events...\n" );

	for ( ;; )
	{	
		int retval;
		fd_set rfds;
		struct timeval tv;

		FD_ZERO( &rfds );
		FD_SET( fd, &rfds );
		FD_SET( uifd, &rfds );

		tv.tv_sec = 0;
		tv.tv_usec = KEY_TIMEOUT;

		retval = select( max( fd, uifd ) + 1, &rfds, NULL, NULL,
			 expecting != KEY ? &tv : NULL  );
			 
		if ( retval == -1 )
			perror("select()");
		else
		if ( retval )
		{
			/* Input is waiting */

			/* Handle upstream input (LED, REP) */
			if ( FD_ISSET( uifd, &rfds ) )
			{
				fprintf( stderr, "Sending event upstream..\n" );
				read( uifd, &iev, sizeof( iev ) );
				write( fd, &iev, sizeof ( iev ) );
			}

			/* Handle keyboard input */
			if ( FD_ISSET( fd, &rfds ) )
			{
				read( fd, &iev, sizeof( iev ) );

				switch ( iev.type )
				{
					case EV_KEY:
						key = iev.code;
						value = iev.value;
						continue;
						break;
					case EV_MSC:
						if ( iev.code == MSC_SCAN )
							scancode = iev.value;
						continue;
						break;
					case EV_SYN:
						if ( iev.code != SYN_REPORT )
						{
							fprintf( stderr, "Unknown event type!\n" );
							continue;
						}
						break;
					default:
						continue;
				}

				iev.type = EV_KEY;
							
				if ( key >= 0 )
				{
					iev.code = key;
					iev.value = value;
				}
				else
				{
					iev.code = scancode;
					iev.value = 2;
				}

				scancode = value = key = -1;

		loop:

				switch ( expecting )
				{
					case KEY:

						if ( iskey( iev.code ) )
						{
							prev_iev = iev;
							expecting = VELOCITY;
						}
						else
						if ( iev.code == KEY_F9 )
						{
							quaver_sec = iev.time.tv_sec;
							prog_mode = MUSIC;
						}
						else
						if ( ( iev.time.tv_sec - quaver_sec )
								<= FUNCTION_TIMEOUT )
						{
							if ( func_key( iev.code ) )
								quaver_sec = iev.time.tv_sec;
							else
								/* can't be a piano key, pass it */
								send_key( &iev );
						}
						else
							/* can't be a piano key, pass it */
							send_key( &iev );

					
						break;
					case VELOCITY:

						expecting = KEY;

						if ( iskey( iev.code ) )
						{
							send_key( &prev_iev );

							goto loop;
						}
						else
						if ( isnum( iev.code ) )
						{
							snd_seq_ev_clear( &ev );
		

							switch ( prog_mode )
							{

								case PATCH:
									patch = max( keymap[ prev_iev.code ], 31 ) +
										( 32 * patch_page );


									snd_seq_ev_set_pgmchange( &ev, channel, patch );
									prog_mode = MUSIC;
									break;
								case BANK:
									bank = max( keymap[ prev_iev.code ], 31 ) +
										( 32 * bank_page );

									snd_seq_ev_set_controller( &ev, channel, 0, bank );
									prog_mode = MUSIC;
									break;

								default:
								{

									/* This MUST be a piano key! */
									int note = ( keymap[ prev_iev.code ] - 19 ) + ( 12 * octave );
									int velocity = nummap[ iev.code ];


#if 0
									notemap[ keymap[ prev_iev.code ] ] = velocity == 0 ? '-' : '0' + velocity;

									notemap[37] = '\0';

									printf( "\r[%s]", notemap );
									fflush( stdout );
#endif

									/* 0 = off, 7 = softest, 1 = hardest (insane, I know) */
									velocity = ! velocity ? 0 : 127 / velocity;
									
									if ( no_velocity )
										velocity = 64;

									/* finally, generate a noteon */
									snd_seq_ev_set_noteon( &ev, channel, note, velocity );
									break;
								}

							}

							send_event( &ev );

							prev_iev = iev;
							expecting = KEY;
						}
						else
						{
							send_key( &prev_iev );

							goto loop;
						}
						
						break;
				}

			}

		}
		else
		{
			/* select timed out. No input to read */

			if ( expecting == VELOCITY )
			{
				expecting = KEY;

				send_key( &prev_iev );
			}
		}

	}
}

