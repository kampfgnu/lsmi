
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <alsa/asoundlib.h>

extern snd_seq_t *seq;
extern int port;
extern int verbose;

/** 
 * register client with ALSA
 */
snd_seq_t *
open_client ( const char *name )
{
	snd_seq_t *handle;
	int err;
	err = snd_seq_open( &handle, "default", SND_SEQ_OPEN_OUTPUT, 0 );
	if ( err < 0 )
		return NULL;
	snd_seq_set_client_name( handle, name );
	return handle;
}

/**
 * Open an output port and return the ID
 */
int
open_output_port ( snd_seq_t *handle )
{
	return snd_seq_create_simple_port( handle, "Output",
			   SND_SEQ_PORT_CAP_READ |
			   SND_SEQ_PORT_CAP_SUBS_READ,
			   SND_SEQ_PORT_TYPE_MIDI_GENERIC |
			   SND_SEQ_PORT_TYPE_APPLICATION );
}

/** 
 * Send sequencer event pointed to by /ev/ to open port without delay.
 */
void
send_event ( snd_seq_event_t *ev )
{
		snd_seq_ev_set_direct( ev );
		snd_seq_ev_set_source( ev, port );
		snd_seq_ev_set_subs( ev );
		snd_seq_event_output_direct( seq, ev );

		if ( verbose == 1 ) 
		{	
			switch ( ev->type )
			{
				case SND_SEQ_EVENT_NOTEON:
					if ( ev->data.note.velocity )
					{
						printf( "Note On: %i..%i\n",
								ev->data.note.note, ev->data.note.velocity );

						break;
					}
				case SND_SEQ_EVENT_NOTEOFF:
					printf( "Note Off: %i\n", ev->data.note.note );
					break;
				case SND_SEQ_EVENT_CONTROLLER:
					printf( "Conntrol Change: %i:%i\n",
							ev->data.control.param, ev->data.control.value );
					break;
				case SND_SEQ_EVENT_PGMCHANGE:
					printf( "Program Change: %i\n", ev->data.control.value );
					break;
					
			}
		}
}


