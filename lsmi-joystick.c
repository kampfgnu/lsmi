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

/* lsmi-joystick.c
 * 
 * Linux Pseudo MIDI Input -- Joystick
 *
 * March, 2007
 *
 * This driver allows any joystick supported by the Linux joydev interface to
 * be used as a MIDI pitch/modulation controller. Of course, some joysticks are
 * more suitable than others. I use an old analog flight stick. Holding down
 * button 1 causes the vertical axis to send pitchbend messages, while button 2
 * causes the vertical axis to send modulation messages. Holding down both
 * buttons causes the vertical axis to send pitchbend messages and the
 * horizontal axis to send modulation messages. 
 *
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
#include <linux/joystick.h>

#include <sys/time.h>
#include <signal.h>
#include <getopt.h>

#include "seq.h"
#include "sig.h"

#define elementsof(x) ( sizeof( (x) ) / sizeof( (x)[0] ) )
#define min(x,min) ( (x) < (min) ? (min) : (x) )
#define max(x,max) ( (x) > (max) ? (max) : (x) )

#define CLIENT_NAME "Pseudo-MIDI Pitch/Mod-Wheel"
#define VERSION "0.1"
#define DOWN 1
#define UP 0

/* global options */
int verbose = 0;
int channel = 0;
int nohold = 0;
int daemonize = 0;

char defaultjoydevice[] = "/dev/input/js0";
char *joydevice = defaultjoydevice;
int jfd;

snd_seq_t *seq = NULL;
int port;

char *sub_name;										/* subscriber */


void
clean_up( void )
{
  close( jfd );
}

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
	fprintf( stderr, "Usage: lsmi-joystick [options]\n"
	"Options:\n\n"
		" -h | --help                   Show this message\n"
		" -d | --device specialfile     Event device to use (instead of js0)\n"
		" -v | --verbose                Be verbose (show note events)\n"
		" -p | --port client:port       Connect to ALSA Sequencer client on startup\n"					
		" -n | --no-hold                Send controller data even when no joystick button is held\n" );
	fprintf( stderr, 	" -z | --daemon                 Fork and don't print anything to stdout\n"
	"\n" );
}

/**
 * process commandline arguments
 */
void
get_args ( int argc, char **argv )
{
	const char *short_opts = "hp:c:vd:nz";
	const struct option long_opts[] =
	{
		{ "help", no_argument, NULL, 'h' },
		{ "port", required_argument, NULL, 'p' },
		{ "channel", required_argument, NULL, 'c' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "device", required_argument, NULL, 'd' },
		{ "no-hold", no_argument, NULL, 'n' },
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
				joydevice = optarg;
				break;
			case 'n':
				nohold = 1;
				break;
			case 'z':
				daemonize = 1;
				break;
		}

	}
}

/** main 
 *
 */
int
main ( int argc, char **argv )
{
	fprintf( stderr, "lsmi-joystick" " v" VERSION "\n" );

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

	fprintf( stderr, "Initializing joystick...\n" );

	if ( -1 == ( jfd = open( joydevice, O_RDONLY ) ) )
	{
		fprintf( stderr, "Error opening event interface! (%s)\n", strerror( errno ) );
		clean_up();
		exit(1);
	}

	set_traps();

	fprintf( stderr, "Waiting for events...\n" );

	for ( ;; )
	{
		struct js_event e;
		snd_seq_event_t ev;
		static int b1;
		static int b2;

		read (jfd, &e, sizeof(struct js_event));

		snd_seq_ev_clear( &ev );

		switch (e.type)
		{
			case JS_EVENT_BUTTON:
				switch (e.number)
				{
					case 0:
						if(e.value)
							b1 = 1;
						else
						{
							b1 = 0;
							snd_seq_ev_set_pitchbend( &ev, channel, 0 );

							send_event( &ev );
						}
						break;
					case 1:
						if (e.value)
							b2 = 1;
						else
						{
							b2 = 0;
							snd_seq_ev_set_controller( &ev, channel, 1, 0 );
							send_event( &ev );
							snd_seq_ev_set_controller( &ev, channel, 33, 0 );
							send_event( &ev );
						}
						break;
				}
				break;
			case JS_EVENT_AXIS:
				
				if ( e.number == 1 && ( b1 || nohold ) )
				{
					snd_seq_ev_set_pitchbend( &ev, channel, 0 - (int)((e.value) * ((float)8191/32767) ));

					send_event( &ev );
				}
				else
				if ( ( e.number == 1 && b2 ) ||
					 ( e.number == 0 && ( ( b1 && b2 ) || nohold ) )
				)
				{
					int fine = (int)((0 - e.value) + 32767) * ((float)16383/65534);
					int	course = fine >> 7;
					fine &= 0x7F;

					snd_seq_ev_set_controller( &ev, channel, 1, course );
					send_event( &ev );
					snd_seq_ev_set_controller( &ev, channel, 33, fine );
					send_event( &ev );
				}
				break;

			 default:
				break;
		}

	}
}

