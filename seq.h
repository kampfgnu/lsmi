
snd_seq_t * open_client __P(( const char *name ));
int open_output_port __P(( snd_seq_t *handle ));
void send_event __P(( snd_seq_event_t *ev ));

