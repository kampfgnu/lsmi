
! title		Linux pSeudo MIDI Input
! author	Jonathan Moore Liles #(email,wantingwaiting@users.sf.net)
! date		April 2007
! revision	0.2
! extra		#(url,Home,http://lsmi-all.sf.net)
! keywords  QJackCtl, MIDI, PIC

--

; Description

  These simple user-space drivers support using certain homemade, repurposed,
  or commercial devices as MIDI controllers on Linux, even though the devices
  themselves are not capable of generating MIDI messages; this does not include
  things like MIDI-over-serial or PIC based projects, which are true MIDI
  devices.

  Reasons for using this software include: achieving MIDI entry on machines
  without MIDI ports, reusing old hardware, pure frugality, and fun.

  The high retail price of even the simplest MIDI keyboards is totally
  incongruent with the level of technology involved, or the quality of the
  construction. The average TV remote control or toy transistor radio
  represents orders of magnitude more sophistication at a tiny fraction of the
  price.  Musical keyboards that don't speak MIDI (toys or antiques) can be
  found at little or no cost and adapted for use with Linux. Mice can be used
  as foot controllers/pedal boards, old analog joysticks as pitch/mod wheels.
  Clunky, clicky QWERTY keyboards as musical keyboards and so on. Once
  connected to this software these devices will be indistinguishable from real
  MIDI hardware. If your computer has a MIDI port, you can even route the
  messages out to control real synth modules or be recorded in your favorite
  sequencer on your Atari ST.

  I wrote this software for myself. That is to say, I own and use all the
  devices it supports. Your needs may differ substantially. Feel free to adapt
  the code as necessary. It is assumed that users have a working knowledge of
  MIDI/audio under Linux and can setup and route through the ALSA Sequencer
  interface.

  Each of the drivers utilizes the Linux input event interface and monopolizes
  its attached device, except for lsmi-monterey, which filters out musical
  events and passes textual key-presses on to applications. There is no
  dependence on X, necessity for window focus, etc.

  For specific information, see the comments at the heads of the respective
  source files.

; Available Drivers

	keyhack
		Hacked AT / PS/2 keyboard controller as MIDI keyboard.
	joystick
		Unmodified two-button joystick as MIDI pitchbend and modulation wheel.
	mouse
		Hacked mouse as MIDI footswitch / pedal controller.
	monterey
		Driver for Monterey International MK-9500 / K617W reversible keyboard (QWERTY on top, 37 piano keys on reverse.)

; Prerequisites

  Projects shouldn't be dwarfed by the autoconf scripts required to build them.
  Therefore, LSMI is distributed with a very simple makefile; you'll have to
  ensure that you have the appropriate kernel and alsa-lib headers installed
  before building.
  
; Usage

  Distribution specific init scripts are not included. The drivers may be
  started from init, your `.bashrc`, by QJackCtl, etc. In order to be run by a
  non-root user the drivers must have access to the device files in
  `/dev/input.`  This may be accomplished by adding a group 'input', adding
  desired users to this group, and configuring /udev/ to assign the appropriate
  ownership to files in `/dev/input.` It should be resonably safe to run the
  drivers as root, however.

  Likewise, for realtime scheduling you must add lines to
  #(c,/etc/security/limits.conf) to allow a certain user or group to change rt
  priorities (this is probably already the case on a machine set up for Jack.)

