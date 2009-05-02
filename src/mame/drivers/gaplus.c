/***************************************************************************

Gaplus (c) 1984 Namco

driver by Manuel Abadia, Ernesto Corvi, Nicola Salmoria


Custom ICs:
----------
11XX     gfx data shifter and mixer (16-bit in, 4-bit out) [1]
15XX     sound control
16XX     I/O control
CUS20    tilemap and sprite address generator
CUS21    sprite generator
CUS26    starfield generator
CUS29    sprite line buffer and sprite/tilemap mixer
CUS33    timing generator
CUS34    address decoder
56XX     I/O
58XX     I/O
CUS62    I/O and explosion generator
98XX     lamp/coin output
99XX     sound volume


memory map
----------
Most of the address decoding for main and sound CPU is done by a custom IC (34XX),
so the memory map is largely deducted by program behaviour. The 34XX also handles
internally the main and sub irq, and a watchdog.
Most of the address decoding for sub CPU is done by a PAL which was read and
decoded, but there are some doubts about its validity.
There is also some additional decoding for tile/sprite RAM done by the 20XX
tilemap and sprite address generator.

Note: chip positions are based on the Midway version schematics. The Namco
version has a different layout (see later for the known correspondencies)

MAIN CPU:

Address          Dir Data     Name      Description
---------------- --- -------- --------- -----------------------
00000xxxxxxxxxxx R/W xxxxxxxx RAM 9J    tilemap RAM (shared with sub CPU)
00001xxxxxxxxxxx R/W xxxxxxxx RAM 3M    work RAM (shared with sub CPU)
000011111xxxxxxx R/W xxxxxxxx           portion holding sprite registers (sprite number & color)
00010xxxxxxxxxxx R/W xxxxxxxx RAM 3K    work RAM (shared with sub CPU)
000101111xxxxxxx R/W xxxxxxxx           portion holding sprite registers (x, y)
00011xxxxxxxxxxx R/W xxxxxxxx RAM 3L    work RAM (shared with sub CPU)
000111111xxxxxxx R/W xxxxxxxx           portion holding sprite registers (x msb, flip, size)
01100-xxxxxxxxxx R/W xxxxxxxx SOUND     RAM (shared with sound CPU)
01101-----xxxxxx R/W ----xxxx FBIT      I/O chips
0111x-----------   W --------           main CPU irq enable (data is in A11) (MIRQ generated by 34XX)
01111----------- R   --------           watchdog reset (MRESET generated by 34XX)
1000x-----------   W -------- SRESET    reset sub and sound CPU, sound enable (data is in A11) (latch in 34XX)
1001x-----------   W -------- FRESET    reset I/O chips (data is in A11) (latch in 34XX)
10100---------xx   W xxxxxxxx STWR      to custom 26XX (starfield control)
10-xxxxxxxxxxxxx R   xxxxxxxx ROM 9E    program ROM (can optionally be a 27128)
110xxxxxxxxxxxxx R   xxxxxxxx ROM 9D    program ROM
111xxxxxxxxxxxxx R   xxxxxxxx ROM 9C    program ROM

[1] Program uses addresses with A10 = 1, e.g. 7400, 7c00, but A10 is not used.
On startup, it also writes to 7820-782f. This might be a bug, the intended range
being 6820-682f to address the 3rd I/O chip.


SOUND CPU:

Address          Dir Data     Name      Description
---------------- --- -------- --------- -----------------------
000---xxxxxxxxxx R/W xxxxxxxx SOUND2    RAM (shared with main CPU)
001------------- R/W --------           watchdog reset? (34XX) [1]
01x-------------   W --------           sound CPU irq enable (data is in A13) (SIRQ generated by 34XX)
11-xxxxxxxxxxxxx R   xxxxxxxx ROM 7B    program ROM (can optionally be a 27128)

[1] Program writes to 3000 and on startup reads from 3000.
On startup it also writes to 2007, but there doesn't seem to be anything else there.


SUB CPU:

Address          Dir Data     Name      Description
---------------- --- -------- --------- -----------------------
00000xxxxxxxxxxx R/W xxxxxxxx RAM 9J    tilemap RAM (shared with main CPU)
00001xxxxxxxxxxx R/W xxxxxxxx RAM 3M    work RAM (shared with main CPU)
000011111xxxxxxx R/W xxxxxxxx           portion holding sprite registers (sprite number & color)
00010xxxxxxxxxxx R/W xxxxxxxx RAM 3K    work RAM (shared with main CPU)
000101111xxxxxxx R/W xxxxxxxx           portion holding sprite registers (x, y)
00011xxxxxxxxxxx R/W xxxxxxxx RAM 3L    work RAM (shared with main CPU)
000111111xxxxxxx R/W xxxxxxxx           portion holding sprite registers (x msb, flip, size)
0110-----------x     -------- VINTON    sub CPU irq enable (data is in A0) [1]
10-xxxxxxxxxxxxx R   xxxxxxxx ROM 6L    program ROM (can optionally be a 27128)
110xxxxxxxxxxxxx R   xxxxxxxx ROM 6M    program ROM
111xxxxxxxxxxxxx R   xxxxxxxx ROM 6N    program ROM

[1] Program normally uses 6080/6081, but 6001 is written on startup.
500F is also written on startup, whose meaning is unknown.


ROM chip placements
-------------------
Midway  Namco
------  -----
9C      8B
9D      8C
9E      8D
6N      11B
6M      11C
6L      11D
7B      4B
9L      8S
5K      11R
5L      11N
5M      11P
5N      11M

----------------------------------------------------------------------------


Notes:
------
- Easter egg:
  - enter service mode
  - keep P1 start and P1 button pressed
  - move joystick left until sound reaches 19
  (c) 1984 NAMCO will appear on the screen

- most sets always say "I/O OK", even if the custom I/O checks fail. Only
  gapluso and gaplusa stop working; these two also don't do the usual
  Namco-trademark RAM test on startup, and use the first I/O chip in "coin" mode,
  while the others use it in "switch/lamp" mode.

- gaplusa has the 58XX and 56XX inverted. Why would they do that?

- To use Round Advance: turn the dip switch on before the start of a level. Push
  joystick up to pick a later level, then set the dip switch back to off.

- The only difference between galaga3a and galaga3m is the bonus life settings.

TODO:
- The starfield is wrong.

- schematics show 4 lines going from the 58XX I/O chip to the 26XX (starfield generator).
  Function and operation unknown.

- Add 62XX custom to machine/namcoio.c (though it's quite different from 56XX and 58XX).

- Is the spirte generator the same as Phozon? This isn't clear yet. They are
  very similar, especially in the way the size flags are layed out.

***************************************************************************/

#include "driver.h"
#include "cpu/m6809/m6809.h"
#include "machine/namcoio.h"
#include "sound/namco.h"
#include "sound/samples.h"
#include "includes/gaplus.h"


/***************************************************************************

  Custom I/O initialization

***************************************************************************/

static READ8_HANDLER( in0_l )	{ return input_port_read(space->machine, "IN0"); }			// P1 joystick
static READ8_HANDLER( in0_h )	{ return input_port_read(space->machine, "IN0") >> 4; }	// P2 joystick
static READ8_HANDLER( in1_l )	{ return input_port_read(space->machine, "IN1"); }			// fire and start buttons
static READ8_HANDLER( in1_h )	{ return input_port_read(space->machine, "IN1") >> 4; }	// coins
static READ8_HANDLER( dipA_l )	{ return input_port_read(space->machine, "DSW0"); }		// dips A
static READ8_HANDLER( dipA_h )	{ return input_port_read(space->machine, "DSW0") >> 4; }	// dips A
static READ8_HANDLER( dipB_l )	{ return input_port_read(space->machine, "DSW1"); }		// dips B
static READ8_HANDLER( dipB_h )	{ return input_port_read(space->machine, "DSW1") >> 4; }	// dips B
static WRITE8_HANDLER( out_lamps0 )
{
	set_led_status(0,data & 1);
	set_led_status(1,data & 2);
	coin_lockout_global_w(data & 4);
	coin_counter_w(0,~data & 8);
}
static WRITE8_HANDLER( out_lamps1 )
{
	coin_counter_w(1,~data & 1);
}

/* chip #0: player inputs, buttons, coins */
static const struct namcoio_interface intf0 =
{
	{ in1_h, in0_l, in0_h, in1_l },	/* port read handlers */
	{ NULL, NULL }		/* port write handlers */
};
static const struct namcoio_interface intf0_lamps =
{
	{ in1_h, in0_l, in0_h, in1_l },	/* port read handlers */
	{ out_lamps0, out_lamps1 }		/* port write handlers */
};
/* chip #1: dip switches */
static const struct namcoio_interface intf1 =
{
	{ dipA_h, dipB_l, dipB_h, dipA_l },	/* port read handlers */
	{ NULL, NULL }						/* port write handlers */
};
/* TODO: chip #2: test/cocktail, optional buttons */

static void unpack_gfx(running_machine *machine);

static DRIVER_INIT( 56_58 )
{
	unpack_gfx(machine);
	namcoio_init(machine, 0, NAMCOIO_56XX, &intf0);
	namcoio_init(machine, 1, NAMCOIO_58XX, &intf1);
}

static DRIVER_INIT( 56_58l )
{
	unpack_gfx(machine);
	namcoio_init(machine, 0, NAMCOIO_56XX, &intf0_lamps);
	namcoio_init(machine, 1, NAMCOIO_58XX, &intf1);
}

static DRIVER_INIT( 58_56 )
{
	unpack_gfx(machine);
	namcoio_init(machine, 0, NAMCOIO_58XX, &intf0);
	namcoio_init(machine, 1, NAMCOIO_56XX, &intf1);
}


/***************************************************************************/


static READ8_HANDLER( gaplus_spriteram_r )
{
    return gaplus_spriteram[offset];
}

static WRITE8_HANDLER( gaplus_spriteram_w )
{
    gaplus_spriteram[offset] = data;
}

static READ8_HANDLER( gaplus_snd_sharedram_r )
{
    return namco_soundregs[offset];
}

static WRITE8_DEVICE_HANDLER( gaplus_snd_sharedram_w )
{
	if (offset < 0x40)
		namco_15xx_w(device,offset,data);
	else
		namco_soundregs[offset] = data;
}


static WRITE8_HANDLER( gaplus_irq_1_ctrl_w )
{
	int bit = !BIT(offset,11);
	cpu_interrupt_enable(space->machine->cpu[0],bit);
	if (!bit)
		cpu_set_input_line(space->machine->cpu[0], 0, CLEAR_LINE);
}

static WRITE8_HANDLER( gaplus_irq_3_ctrl_w )
{
	int bit = !BIT(offset,13);
	cpu_interrupt_enable(space->machine->cpu[2],bit);
	if (!bit)
		cpu_set_input_line(space->machine->cpu[2], 0, CLEAR_LINE);
}

static WRITE8_HANDLER( gaplus_irq_2_ctrl_w )
{
	int bit = offset & 1;
	cpu_interrupt_enable(space->machine->cpu[1],bit);
	if (!bit)
		cpu_set_input_line(space->machine->cpu[1], 0, CLEAR_LINE);
}

static WRITE8_HANDLER( gaplus_sreset_w )
{
	int bit = !BIT(offset,11);
    cpu_set_input_line(space->machine->cpu[1], INPUT_LINE_RESET, bit ? CLEAR_LINE : ASSERT_LINE);
    cpu_set_input_line(space->machine->cpu[2], INPUT_LINE_RESET, bit ? CLEAR_LINE : ASSERT_LINE);
	mappy_sound_enable(devtag_get_device(space->machine, "namco"), bit);
}

static WRITE8_HANDLER( gaplus_freset_w )
{
	int bit = !BIT(offset,11);
logerror("%04x: freset %d\n",cpu_get_pc(space->cpu),bit);
	namcoio_set_reset_line(0, bit ? CLEAR_LINE : ASSERT_LINE);
	namcoio_set_reset_line(1, bit ? CLEAR_LINE : ASSERT_LINE);
}

static MACHINE_RESET( gaplus )
{
	/* on reset, VINTON is reset, while the other flags don't seem to be affected */
	cpu_interrupt_enable(machine->cpu[1],0);
	cpu_set_input_line(machine->cpu[1], 0, CLEAR_LINE);
}

static INTERRUPT_GEN( gaplus_interrupt_1 )
{
	irq0_line_assert(device);	// this also checks if irq is enabled - IMPORTANT!
						// so don't replace with cpu_set_input_line(machine->cpu[0], 0, ASSERT_LINE);

	namcoio_set_irq_line(device->machine,0,PULSE_LINE);
	namcoio_set_irq_line(device->machine,1,PULSE_LINE);
}


static ADDRESS_MAP_START( cpu1_map, ADDRESS_SPACE_PROGRAM, 8 )
	AM_RANGE(0x0000, 0x07ff) AM_READWRITE(gaplus_videoram_r, gaplus_videoram_w) AM_BASE(&gaplus_videoram)		/* tilemap RAM (shared with CPU #2) */
	AM_RANGE(0x0800, 0x1fff) AM_READWRITE(gaplus_spriteram_r, gaplus_spriteram_w) AM_BASE(&gaplus_spriteram)	/* shared RAM with CPU #2 (includes sprite RAM) */
	AM_RANGE(0x6000, 0x63ff) AM_READ(gaplus_snd_sharedram_r) 													/* shared RAM with CPU #3 */
	AM_RANGE(0x6000, 0x63ff) AM_DEVWRITE("namco", gaplus_snd_sharedram_w) 										/* shared RAM with CPU #3 */
	AM_RANGE(0x6820, 0x682f) AM_READWRITE(gaplus_customio_3_r, gaplus_customio_3_w) AM_BASE(&gaplus_customio_3)	/* custom I/O chip #3 interface */
	AM_RANGE(0x6800, 0x6bff) AM_READWRITE(namcoio_r, namcoio_w)													/* custom I/O chips interface */
	AM_RANGE(0x7000, 0x7fff) AM_WRITE(gaplus_irq_1_ctrl_w)														/* main CPU irq control */
	AM_RANGE(0x7800, 0x7fff) AM_READ(watchdog_reset_r)															/* watchdog */
	AM_RANGE(0x8000, 0x8fff) AM_WRITE(gaplus_sreset_w)	 														/* reset CPU #2 & #3, enable sound */
	AM_RANGE(0x9000, 0x9fff) AM_WRITE(gaplus_freset_w)	 														/* reset I/O chips */
	AM_RANGE(0xa000, 0xa7ff) AM_WRITE(gaplus_starfield_control_w)				/* starfield control */
	AM_RANGE(0xa000, 0xffff) AM_ROM																				/* ROM */
ADDRESS_MAP_END

static ADDRESS_MAP_START( cpu2_map, ADDRESS_SPACE_PROGRAM, 8 )
	AM_RANGE(0x0000, 0x07ff) AM_READWRITE(gaplus_videoram_r, gaplus_videoram_w)		/* tilemap RAM (shared with CPU #1) */
	AM_RANGE(0x0800, 0x1fff) AM_READWRITE(gaplus_spriteram_r, gaplus_spriteram_w)	/* shared RAM with CPU #1 */
//  AM_RANGE(0x500f, 0x500f) AM_WRITENOP             								/* ??? written 256 times on startup */
	AM_RANGE(0x6000, 0x6fff) AM_WRITE(gaplus_irq_2_ctrl_w)							/* IRQ 2 control */
	AM_RANGE(0xa000, 0xffff) AM_ROM													/* ROM */
ADDRESS_MAP_END

static ADDRESS_MAP_START( cpu3_map, ADDRESS_SPACE_PROGRAM, 8 )
	AM_RANGE(0x0000, 0x03ff) AM_READ(gaplus_snd_sharedram_r) 										/* shared RAM with CPU #1 */
	AM_RANGE(0x0000, 0x03ff) AM_DEVWRITE("namco", gaplus_snd_sharedram_w) AM_BASE(&namco_soundregs)	/* shared RAM with the main CPU + sound registers */
	AM_RANGE(0x2000, 0x3fff) AM_READWRITE(watchdog_reset_r, watchdog_reset_w)						/* watchdog? */
	AM_RANGE(0x4000, 0x7fff) AM_WRITE(gaplus_irq_3_ctrl_w)											/* interrupt enable/disable */
	AM_RANGE(0xe000, 0xffff) AM_ROM																	/* ROM */
ADDRESS_MAP_END



static INPUT_PORTS_START( gaplus )
	/* The inputs are not memory mapped, they are handled by three I/O chips. */
	PORT_START("IN0")	/* 56XX #0 pins 22-29 */
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP ) PORT_8WAY
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_8WAY
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN ) PORT_8WAY
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT ) PORT_8WAY
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_JOYSTICK_UP ) PORT_8WAY PORT_COCKTAIL
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_8WAY PORT_COCKTAIL
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN ) PORT_8WAY PORT_COCKTAIL
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT ) PORT_8WAY PORT_COCKTAIL

	PORT_START("IN1")	/* 56XX #0 pins 30-33 and 38-41 */
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON1 )
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_COCKTAIL
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_START1 )
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_START2 )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_COIN1 )
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_COIN2 )
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_SERVICE1 )

	PORT_START("DSW0")	/* 58XX #1 pins 30-33 and 38-41 */
	PORT_DIPNAME( 0xc0, 0xc0, DEF_STR( Lives ) )
	PORT_DIPSETTING(    0x80, "2" )
	PORT_DIPSETTING(    0xc0, "3" )
	PORT_DIPSETTING(    0x40, "4" )
	PORT_DIPSETTING(    0x00, "5" )
	PORT_DIPNAME( 0x30, 0x30, DEF_STR( Coin_A ) )
	PORT_DIPSETTING(    0x00, DEF_STR( 3C_1C ) )
	PORT_DIPSETTING(    0x10, DEF_STR( 2C_1C ) )
	PORT_DIPSETTING(    0x30, DEF_STR( 1C_1C ) )
	PORT_DIPSETTING(    0x20, DEF_STR( 1C_2C ) )
	PORT_DIPNAME( 0x08, 0x08, DEF_STR( Demo_Sounds ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x08, DEF_STR( On ) )
	PORT_DIPNAME( 0x04, 0x04, DEF_STR( Unused ) )
	PORT_DIPSETTING(    0x04, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x03, 0x03, DEF_STR( Coin_B ) )
	PORT_DIPSETTING(    0x00, DEF_STR( 3C_1C ) )
	PORT_DIPSETTING(    0x01, DEF_STR( 2C_1C ) )
	PORT_DIPSETTING(    0x03, DEF_STR( 1C_1C ) )
	PORT_DIPSETTING(    0x02, DEF_STR( 1C_2C ) )

	PORT_START("DSW1")	/* 58XX #1 pins 22-29 */
	PORT_DIPNAME( 0x80, 0x80, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x80, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x70, 0x70, DEF_STR( Difficulty ) )
	PORT_DIPSETTING(    0x70, "0 - Standard" )
	PORT_DIPSETTING(    0x60, "1 - Easiest" )
	PORT_DIPSETTING(    0x50, "2" )
	PORT_DIPSETTING(    0x40, "3" )
	PORT_DIPSETTING(    0x30, "4" )
	PORT_DIPSETTING(    0x20, "5" )
	PORT_DIPSETTING(    0x10, "6" )
	PORT_DIPSETTING(    0x00, "7 - Hardest" )
	PORT_DIPNAME( 0x08, 0x08, "Round Advance" )
	PORT_DIPSETTING(    0x08, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x07, 0x00, DEF_STR( Bonus_Life ) )
	PORT_DIPSETTING(    0x00, "30k 70k and every 70k" )
	PORT_DIPSETTING(    0x01, "30k 100k and every 100k" )
	PORT_DIPSETTING(    0x02, "30k 100k and every 200k" )
	PORT_DIPSETTING(    0x03, "50k 100k and every 100k" )
	PORT_DIPSETTING(    0x04, "50k 100k and every 200k" )
	PORT_DIPSETTING(    0x07, "50k 150k and every 150k" )
	PORT_DIPSETTING(    0x05, "50k 150k and every 300k" )
	PORT_DIPSETTING(    0x06, "50k 150k" )

	PORT_START("IN2")	/* 62XX #2 pins 24-27 */
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_DIPNAME( 0x04, 0x04, DEF_STR( Cabinet ) )
	PORT_DIPSETTING(    0x04, DEF_STR( Upright ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Cocktail ) )
	PORT_SERVICE( 0x08, IP_ACTIVE_LOW )
INPUT_PORTS_END

/* identical to gaplus, but service mode is a dip switch instead of coming from edge connector */
static INPUT_PORTS_START( gapluso )
	PORT_INCLUDE( gaplus )

	PORT_MODIFY("DSW1")
	PORT_SERVICE( 0x80, IP_ACTIVE_LOW )

	PORT_MODIFY("IN2")
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_UNKNOWN )	// doesn't seem to be used
INPUT_PORTS_END

/* identical to gaplus, but different bonus life settings */
static INPUT_PORTS_START( galaga3a )
	PORT_INCLUDE( gaplus )

	PORT_MODIFY("DSW1")
	PORT_DIPNAME( 0x07, 0x02, DEF_STR( Bonus_Life ) )
	PORT_DIPSETTING(    0x02, "30k 80k and every 100k" )
	PORT_DIPSETTING(    0x03, "30k 100k and every 100k" )
	PORT_DIPSETTING(    0x04, "30k 100k and every 150k" )
	PORT_DIPSETTING(    0x07, "30k 100k and every 200k" )
	PORT_DIPSETTING(    0x05, "30k 100k and every 300k" )
	PORT_DIPSETTING(    0x06, "30k 150k" )
	PORT_DIPSETTING(    0x00, "50k 150k and every 150k" )
	PORT_DIPSETTING(    0x01, "50k 150k and every 200k" )
INPUT_PORTS_END

/* identical to gaplus, but different bonus life settings */
static INPUT_PORTS_START( galaga3m )
	PORT_INCLUDE( gaplus )

	PORT_MODIFY("DSW1")
	PORT_DIPNAME( 0x07, 0x00, DEF_STR( Bonus_Life ) )
	PORT_DIPSETTING(    0x00, "30k 150k and every 600k" )
	PORT_DIPSETTING(    0x01, "50k 150k and every 300k" )
	PORT_DIPSETTING(    0x02, "50k 150k and every 600k" )
	PORT_DIPSETTING(    0x03, "50k 200k and every 300k" )
	PORT_DIPSETTING(    0x04, "100k 300k and every 300k" )
	PORT_DIPSETTING(    0x07, "100k 300k and every 600k" )
	PORT_DIPSETTING(    0x05, "150k 400k and every 900k" )
	PORT_DIPSETTING(    0x06, "150k 400k" )
INPUT_PORTS_END



static const gfx_layout charlayout =
{
	8,8,
	RGN_FRAC(1,1),
	2,
	{ 4, 6 },
	{ 16*8, 16*8+1, 24*8, 24*8+1, 0, 1, 8*8, 8*8+1 },
	{ 0*8, 1*8, 2*8, 3*8, 4*8, 5*8, 6*8, 7*8 },
	32*8
};

static const gfx_layout spritelayout =
{
	16,16,
	RGN_FRAC(1,2),
	3,
	{ RGN_FRAC(1,2), 0, 4 },
	{ 0, 1, 2, 3, 8*8, 8*8+1, 8*8+2, 8*8+3,
	  16*8+0, 16*8+1, 16*8+2, 16*8+3, 24*8+0, 24*8+1, 24*8+2, 24*8+3 },
	{ 0*8, 1*8, 2*8, 3*8, 4*8, 5*8, 6*8, 7*8,
	  32*8, 33*8, 34*8, 35*8, 36*8, 37*8, 38*8, 39*8 },
	64*8
};

static GFXDECODE_START( gaplus )
	GFXDECODE_ENTRY( "gfx1", 0x0000, charlayout,      0, 64 )
	GFXDECODE_ENTRY( "gfx2", 0x0000, spritelayout, 64*4, 64 )
GFXDECODE_END

static const namco_interface namco_config =
{
	8,	 			/* number of voices */
	0				/* stereo */
};

static const char *const gaplus_sample_names[] =
{
	"*gaplus",
	"bang.wav",
	0
};

static const samples_interface gaplus_samples_interface =
{
	1,	/* one channel */
	gaplus_sample_names
};



static MACHINE_DRIVER_START( gaplus )

	/* basic machine hardware */
	MDRV_CPU_ADD("maincpu", M6809,	24576000/16)	/* 1.536 MHz */
	MDRV_CPU_PROGRAM_MAP(cpu1_map,0)
	MDRV_CPU_VBLANK_INT("screen", gaplus_interrupt_1)

	MDRV_CPU_ADD("sub", M6809,	24576000/16)	/* 1.536 MHz */
	MDRV_CPU_PROGRAM_MAP(cpu2_map,0)
	MDRV_CPU_VBLANK_INT("screen", irq0_line_assert)

	MDRV_CPU_ADD("sub2", M6809, 24576000/16)	/* 1.536 MHz */
	MDRV_CPU_PROGRAM_MAP(cpu3_map,0)
	MDRV_CPU_VBLANK_INT("screen", irq0_line_assert)

	MDRV_QUANTUM_TIME(HZ(6000))	/* a high value to ensure proper synchronization of the CPUs */
	MDRV_MACHINE_RESET(gaplus)

	/* video hardware */
	MDRV_SCREEN_ADD("screen", RASTER)
	MDRV_SCREEN_REFRESH_RATE(60.606060)
	MDRV_SCREEN_VBLANK_TIME(ATTOSECONDS_IN_USEC(0))
	MDRV_SCREEN_FORMAT(BITMAP_FORMAT_INDEXED16)
	MDRV_SCREEN_SIZE(36*8, 28*8)
	MDRV_SCREEN_VISIBLE_AREA(0*8, 36*8-1, 0*8, 28*8-1)

	MDRV_GFXDECODE(gaplus)
	MDRV_PALETTE_LENGTH(64*4+64*8)

	MDRV_PALETTE_INIT(gaplus)
	MDRV_VIDEO_START(gaplus)
	MDRV_VIDEO_UPDATE(gaplus)
	MDRV_VIDEO_EOF(gaplus)

	/* sound hardware */
	MDRV_SPEAKER_STANDARD_MONO("mono")

	MDRV_SOUND_ADD("namco", NAMCO_15XX, 24576000/1024)
	MDRV_SOUND_CONFIG(namco_config)
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "mono", 1.0)

	MDRV_SOUND_ADD("samples", SAMPLES, 0)
	MDRV_SOUND_CONFIG(gaplus_samples_interface)
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "mono", 0.80)
MACHINE_DRIVER_END



ROM_START( gaplus )
	ROM_REGION( 0x10000, "maincpu", 0 ) /* 64k for the MAIN CPU */
	ROM_LOAD( "gp3-4c.8d",    0xa000, 0x2000, CRC(10d7f64c) SHA1(e39f77af16016d28170e4ac1c2a784b0a7ec5454) )
	ROM_LOAD( "gp3-3c.8c",    0xc000, 0x2000, CRC(962411e8) SHA1(2b6bb2a5d77a837810180391ef6c0ce745bfed64) )
	ROM_LOAD( "gp3-2d.8b",    0xe000, 0x2000, CRC(ecc01bdb) SHA1(b176b46bd6f2501d3a74ed11186be8411fd1105b) )

	ROM_REGION( 0x10000, "sub", 0 ) /* 64k for the SUB CPU */
	ROM_LOAD( "gp3-8b.11d",   0xa000, 0x2000, CRC(f5e056d1) SHA1(bbed2056dc28dc2828e29987c16d89fb16e7059e) )
	ROM_LOAD( "gp2-7.11c",    0xc000, 0x2000, CRC(0621f7df) SHA1(b86020f819fefb134cb57e203f7c90b1b29581c8) )
	ROM_LOAD( "gp3-6b.11b",   0xe000, 0x2000, CRC(026491b6) SHA1(a19f2942dafc899d686a42240fc2f7a7a7d3b1f5) )

	ROM_REGION( 0x10000, "sub2", 0 ) /* 64k for the SOUND CPU */
	ROM_LOAD( "gp2-1.4b",     0xe000, 0x2000, CRC(ed8aa206) SHA1(4e0a31d84cb7aca497485dbe0240009d58275765) )

	ROM_REGION( 0x4000, "gfx1", ROMREGION_DISPOSE )
	ROM_LOAD( "gp2-5.8s",     0x0000, 0x2000, CRC(f3d19987) SHA1(a0107fa4659597ac42c875ab1c0deb845534268b) )	/* characters */
	/* 0x2000-0x3fff  will be unpacked from 0x0000-0x1fff */

	ROM_REGION( 0xc000, "gfx2", ROMREGION_DISPOSE )
	ROM_LOAD( "gp2-11.11p",   0x0000, 0x2000, CRC(57740ff9) SHA1(16873e0ac5f975768d596d7d32af7571f4817f2b) )	/* objects */
	ROM_LOAD( "gp2-10.11n",   0x2000, 0x2000, CRC(6cd8ce11) SHA1(fc346e98737c9fc20810e32d4c150ae4b4051979) )	/* objects */
	ROM_LOAD( "gp2-12.11r",   0x4000, 0x2000, CRC(7316a1f1) SHA1(368e4541a5151e906a189712bc05192c2ceec8ae) )	/* objects */
	ROM_LOAD( "gp2-9.11m",    0x6000, 0x2000, CRC(e6a9ae67) SHA1(99c1e67c3b216aa1b63f199e21c73cdedde80e1b) )	/* objects */
	/* 0x8000-0x9fff  will be unpacked from 0x6000-0x7fff */
	ROM_FILL(                 0xa000, 0x2000, 0x00 )	// optional ROM, not used

	ROM_REGION( 0x0800, "proms", 0 )
	ROM_LOAD( "gp2-3.1p",     0x0000, 0x0100, CRC(a5091352) SHA1(dcd6dfbfbd5281ba0c7b7c189d6fde23617ed3e3) )	/* red palette ROM (4 bits) */
	ROM_LOAD( "gp2-1.1n",     0x0100, 0x0100, CRC(8bc8022a) SHA1(c76f9d9b066e268621d41a703c5280261234709a) )	/* green palette ROM (4 bits) */
	ROM_LOAD( "gp2-2.2n",     0x0200, 0x0100, CRC(8dabc20b) SHA1(64d7b333f529d3ba66aeefd380fd1cbf9ddf460d) )	/* blue palette ROM (4 bits) */
	ROM_LOAD( "gp2-7.6s",     0x0300, 0x0100, CRC(2faa3e09) SHA1(781ffe9088476798409cb922350eff881590cf35) )	/* char color ROM */
	ROM_LOAD( "gp2-6.6p",     0x0400, 0x0200, CRC(6f99c2da) SHA1(955dcef363870ee8e91edc73b9ea3ce489738aad) )	/* sprite color ROM (lower 4 bits) */
	ROM_LOAD( "gp2-5.6n",     0x0600, 0x0200, CRC(c7d31657) SHA1(a93a5bc448dc127e1389d10a9cb06acadfe940cf) )	/* sprite color ROM (upper 4 bits) */

	ROM_REGION( 0x0100, "namco", 0 ) /* sound prom */
	ROM_LOAD( "gp2-4.3f",     0x0000, 0x0100, CRC(2d9fbdd8) SHA1(e6a23cd5ce3d3e76de3b70c8ab5a3c45b1147af4) )

	ROM_REGION( 0x0100, "plds", ROMREGION_DISPOSE )
	ROM_LOAD( "pal10l8.8n",   0x0000, 0x002c, CRC(08e5b2fe) SHA1(1aa7fa1a61795703af84ae427d0d8588ef8c4c3f) )
ROM_END

ROM_START( gapluso )
	ROM_REGION( 0x10000, "maincpu", 0 ) /* 64k for the MAIN CPU */
	ROM_LOAD( "gp2-4.8d",     0xa000, 0x2000, CRC(e525d75d) SHA1(93fcd8b940491abf6344181811d0b35765d7e45c) )
	ROM_LOAD( "gp2-3b.8c",    0xc000, 0x2000, CRC(d77840a4) SHA1(81402b28a2d5ac2d1301252534afa0cb65d7e162) )
	ROM_LOAD( "gp2-2b.8b",    0xe000, 0x2000, CRC(b3cb90db) SHA1(025c2f3978772e1ecbbf36842dc7c2203ee91a1f) )

	ROM_REGION( 0x10000, "sub", 0 ) /* 64k for the SUB CPU */
	ROM_LOAD( "gp2-8.11d",    0xa000, 0x2000, CRC(42b9fd7c) SHA1(f230eb0ad757f0714c0ac81c812e950778452947) )
	ROM_LOAD( "gp2-7.11c",    0xc000, 0x2000, CRC(0621f7df) SHA1(b86020f819fefb134cb57e203f7c90b1b29581c8) )
	ROM_LOAD( "gp2-6.11b",    0xe000, 0x2000, CRC(75b18652) SHA1(398059da967c80321a9ec94d982a6c0b3c970c5f) )

	ROM_REGION( 0x10000, "sub2", 0 ) /* 64k for the SOUND CPU */
	ROM_LOAD( "gp2-1.4b",     0xe000, 0x2000, CRC(ed8aa206) SHA1(4e0a31d84cb7aca497485dbe0240009d58275765) )

	ROM_REGION( 0x4000, "gfx1", ROMREGION_DISPOSE )
	ROM_LOAD( "gp2-5.8s",     0x0000, 0x2000, CRC(f3d19987) SHA1(a0107fa4659597ac42c875ab1c0deb845534268b) )	/* characters */
	/* 0x2000-0x3fff  will be unpacked from 0x0000-0x1fff */

	ROM_REGION( 0xc000, "gfx2", ROMREGION_DISPOSE )
	ROM_LOAD( "gp2-11.11p",   0x0000, 0x2000, CRC(57740ff9) SHA1(16873e0ac5f975768d596d7d32af7571f4817f2b) )	/* objects */
	ROM_LOAD( "gp2-10.11n",   0x2000, 0x2000, CRC(6cd8ce11) SHA1(fc346e98737c9fc20810e32d4c150ae4b4051979) )	/* objects */
	ROM_LOAD( "gp2-12.11r",   0x4000, 0x2000, CRC(7316a1f1) SHA1(368e4541a5151e906a189712bc05192c2ceec8ae) )	/* objects */
	ROM_LOAD( "gp2-9.11m",    0x6000, 0x2000, CRC(e6a9ae67) SHA1(99c1e67c3b216aa1b63f199e21c73cdedde80e1b) )	/* objects */
	/* 0x8000-0x9fff  will be unpacked from 0x6000-0x7fff */
	ROM_FILL(                 0xa000, 0x2000, 0x00 )	// optional ROM, not used

	ROM_REGION( 0x0800, "proms", 0 )
	ROM_LOAD( "gp2-3.1p",     0x0000, 0x0100, CRC(a5091352) SHA1(dcd6dfbfbd5281ba0c7b7c189d6fde23617ed3e3) )	/* red palette ROM (4 bits) */
	ROM_LOAD( "gp2-1.1n",     0x0100, 0x0100, CRC(8bc8022a) SHA1(c76f9d9b066e268621d41a703c5280261234709a) )	/* green palette ROM (4 bits) */
	ROM_LOAD( "gp2-2.2n",     0x0200, 0x0100, CRC(8dabc20b) SHA1(64d7b333f529d3ba66aeefd380fd1cbf9ddf460d) )	/* blue palette ROM (4 bits) */
	ROM_LOAD( "gp2-7.6s",     0x0300, 0x0100, CRC(2faa3e09) SHA1(781ffe9088476798409cb922350eff881590cf35) )	/* char color ROM */
	ROM_LOAD( "gp2-6.6p",     0x0400, 0x0200, CRC(6f99c2da) SHA1(955dcef363870ee8e91edc73b9ea3ce489738aad) )	/* sprite color ROM (lower 4 bits) */
	ROM_LOAD( "gp2-5.6n",     0x0600, 0x0200, CRC(c7d31657) SHA1(a93a5bc448dc127e1389d10a9cb06acadfe940cf) )	/* sprite color ROM (upper 4 bits) */

	ROM_REGION( 0x0100, "namco", 0 ) /* sound prom */
	ROM_LOAD( "gp2-4.3f",     0x0000, 0x0100, CRC(2d9fbdd8) SHA1(e6a23cd5ce3d3e76de3b70c8ab5a3c45b1147af4) )

	ROM_REGION( 0x0100, "plds", ROMREGION_DISPOSE )
	ROM_LOAD( "pal10l8.8n",   0x0000, 0x002c, CRC(08e5b2fe) SHA1(1aa7fa1a61795703af84ae427d0d8588ef8c4c3f) )
ROM_END

ROM_START( gaplusa )
	ROM_REGION( 0x10000, "maincpu", 0 ) /* 64k for the MAIN CPU */
	ROM_LOAD( "gp2-4b.8d",    0xa000, 0x2000, CRC(484f11e0) SHA1(659756ae183dac3817440c8975f203c7dbe08c6b) )
	ROM_LOAD( "gp2-3c.8c",    0xc000, 0x2000, CRC(a74b0266) SHA1(a534c6b4af569ed545bf52769c7d5ceb5f2c4935) )
	ROM_LOAD( "gp2-2d.8b",    0xe000, 0x2000, CRC(69fdfdb7) SHA1(aec611336b8767897ad493d581d70b1f0e75aeba) )

	ROM_REGION( 0x10000, "sub", 0 ) /* 64k for the SUB CPU */
	ROM_LOAD( "gp2-8b.11d",   0xa000, 0x2000, CRC(bff601a6) SHA1(e1a04354d8d0bc0d51d7341a46bd23cbd2158ee9) )
	ROM_LOAD( "gp2-7.11c",    0xc000, 0x2000, CRC(0621f7df) SHA1(b86020f819fefb134cb57e203f7c90b1b29581c8) )
	ROM_LOAD( "gp2-6b.11b",   0xe000, 0x2000, CRC(14cd61ea) SHA1(05605abebcf2791e60b2d810dafcdd8582a87d9b) )

	ROM_REGION( 0x10000, "sub2", 0 ) /* 64k for the SOUND CPU */
	ROM_LOAD( "gp2-1.4b",     0xe000, 0x2000, CRC(ed8aa206) SHA1(4e0a31d84cb7aca497485dbe0240009d58275765) )

	ROM_REGION( 0x4000, "gfx1", ROMREGION_DISPOSE )
	ROM_LOAD( "gp2-5.8s",     0x0000, 0x2000, CRC(f3d19987) SHA1(a0107fa4659597ac42c875ab1c0deb845534268b) )	/* characters */
	/* 0x2000-0x3fff  will be unpacked from 0x0000-0x1fff */

	ROM_REGION( 0xc000, "gfx2", ROMREGION_DISPOSE )
	ROM_LOAD( "gp2-11.11p",   0x0000, 0x2000, CRC(57740ff9) SHA1(16873e0ac5f975768d596d7d32af7571f4817f2b) )	/* objects */
	ROM_LOAD( "gp2-10.11n",   0x2000, 0x2000, CRC(6cd8ce11) SHA1(fc346e98737c9fc20810e32d4c150ae4b4051979) )	/* objects */
	ROM_LOAD( "gp2-12.11r",   0x4000, 0x2000, CRC(7316a1f1) SHA1(368e4541a5151e906a189712bc05192c2ceec8ae) )	/* objects */
	ROM_LOAD( "gp2-9.11m",    0x6000, 0x2000, CRC(e6a9ae67) SHA1(99c1e67c3b216aa1b63f199e21c73cdedde80e1b) )	/* objects */
	/* 0x8000-0x9fff  will be unpacked from 0x6000-0x7fff */
	ROM_FILL(                 0xa000, 0x2000, 0x00 )	// optional ROM, not used

	ROM_REGION( 0x0800, "proms", 0 )
	ROM_LOAD( "gp2-3.1p",     0x0000, 0x0100, CRC(a5091352) SHA1(dcd6dfbfbd5281ba0c7b7c189d6fde23617ed3e3) )	/* red palette ROM (4 bits) */
	ROM_LOAD( "gp2-1.1n",     0x0100, 0x0100, CRC(8bc8022a) SHA1(c76f9d9b066e268621d41a703c5280261234709a) )	/* green palette ROM (4 bits) */
	ROM_LOAD( "gp2-2.2n",     0x0200, 0x0100, CRC(8dabc20b) SHA1(64d7b333f529d3ba66aeefd380fd1cbf9ddf460d) )	/* blue palette ROM (4 bits) */
	ROM_LOAD( "gp2-7.6s",     0x0300, 0x0100, CRC(2faa3e09) SHA1(781ffe9088476798409cb922350eff881590cf35) )	/* char color ROM */
	ROM_LOAD( "gp2-6.6p",     0x0400, 0x0200, CRC(6f99c2da) SHA1(955dcef363870ee8e91edc73b9ea3ce489738aad) )	/* sprite color ROM (lower 4 bits) */
	ROM_LOAD( "gp2-5.6n",     0x0600, 0x0200, CRC(c7d31657) SHA1(a93a5bc448dc127e1389d10a9cb06acadfe940cf) )	/* sprite color ROM (upper 4 bits) */

	ROM_REGION( 0x0100, "namco", 0 ) /* sound prom */
	ROM_LOAD( "gp2-4.3f",     0x0000, 0x0100, CRC(2d9fbdd8) SHA1(e6a23cd5ce3d3e76de3b70c8ab5a3c45b1147af4) )

	ROM_REGION( 0x0100, "plds", ROMREGION_DISPOSE )
	ROM_LOAD( "pal10l8.8n",   0x0000, 0x002c, CRC(08e5b2fe) SHA1(1aa7fa1a61795703af84ae427d0d8588ef8c4c3f) )
ROM_END

ROM_START( galaga3 )
	ROM_REGION( 0x10000, "maincpu", 0 ) /* 64k for the MAIN CPU */
	ROM_LOAD( "gp3-4c.8d",    0xa000, 0x2000, CRC(10d7f64c) SHA1(e39f77af16016d28170e4ac1c2a784b0a7ec5454) )
	ROM_LOAD( "gp3-3c.8c",    0xc000, 0x2000, CRC(962411e8) SHA1(2b6bb2a5d77a837810180391ef6c0ce745bfed64) )
	ROM_LOAD( "gp3-2c.8b",    0xe000, 0x2000, CRC(f72d6fc5) SHA1(7031c4a2c4374fb786fc563cbad3e3de0dbaa8d2) )

	ROM_REGION( 0x10000, "sub", 0 ) /* 64k for the SUB CPU */
	ROM_LOAD( "gp3-8b.11d",   0xa000, 0x2000, CRC(f5e056d1) SHA1(bbed2056dc28dc2828e29987c16d89fb16e7059e) )
	ROM_LOAD( "gp2-7.11c",    0xc000, 0x2000, CRC(0621f7df) SHA1(b86020f819fefb134cb57e203f7c90b1b29581c8) )
	ROM_LOAD( "gp3-6b.11b",   0xe000, 0x2000, CRC(026491b6) SHA1(a19f2942dafc899d686a42240fc2f7a7a7d3b1f5) )

	ROM_REGION( 0x10000, "sub2", 0 ) /* 64k for the SOUND CPU */
	ROM_LOAD( "gp2-1.4b",     0xe000, 0x2000, CRC(ed8aa206) SHA1(4e0a31d84cb7aca497485dbe0240009d58275765) )

	ROM_REGION( 0x4000, "gfx1", ROMREGION_DISPOSE )
	ROM_LOAD( "gal3_9l.bin",  0x0000, 0x2000, CRC(8d4dcebf) SHA1(0a556b45976bc36eb99048b1512c446b472da1d2) )	/* characters */
	/* 0x2000-0x3fff  will be unpacked from 0x0000-0x1fff */

	ROM_REGION( 0xc000, "gfx2", ROMREGION_DISPOSE )
	ROM_LOAD( "gp2-11.11p",   0x0000, 0x2000, CRC(57740ff9) SHA1(16873e0ac5f975768d596d7d32af7571f4817f2b) )	/* objects */
	ROM_LOAD( "gp2-10.11n",   0x2000, 0x2000, CRC(6cd8ce11) SHA1(fc346e98737c9fc20810e32d4c150ae4b4051979) )	/* objects */
	ROM_LOAD( "gp2-12.11r",   0x4000, 0x2000, CRC(7316a1f1) SHA1(368e4541a5151e906a189712bc05192c2ceec8ae) )	/* objects */
	ROM_LOAD( "gp2-9.11m",    0x6000, 0x2000, CRC(e6a9ae67) SHA1(99c1e67c3b216aa1b63f199e21c73cdedde80e1b) )	/* objects */
	/* 0x8000-0x9fff  will be unpacked from 0x6000-0x7fff */
	ROM_FILL(                 0xa000, 0x2000, 0x00 )	// optional ROM, not used

	ROM_REGION( 0x0800, "proms", 0 )
	ROM_LOAD( "gp2-3.1p",     0x0000, 0x0100, CRC(a5091352) SHA1(dcd6dfbfbd5281ba0c7b7c189d6fde23617ed3e3) )	/* red palette ROM (4 bits) */
	ROM_LOAD( "gp2-1.1n",     0x0100, 0x0100, CRC(8bc8022a) SHA1(c76f9d9b066e268621d41a703c5280261234709a) )	/* green palette ROM (4 bits) */
	ROM_LOAD( "gp2-2.2n",     0x0200, 0x0100, CRC(8dabc20b) SHA1(64d7b333f529d3ba66aeefd380fd1cbf9ddf460d) )	/* blue palette ROM (4 bits) */
	ROM_LOAD( "gp2-7.6s",     0x0300, 0x0100, CRC(2faa3e09) SHA1(781ffe9088476798409cb922350eff881590cf35) )	/* char color ROM */
	ROM_LOAD( "g3_3f.bin",    0x0400, 0x0200, CRC(d48c0eef) SHA1(6d0512958bc522d22e69336677369507847f8f6f) )	/* sprite color ROM (lower 4 bits) */
	ROM_LOAD( "g3_3e.bin",    0x0600, 0x0200, CRC(417ba0dc) SHA1(2ba51ccdd0428fc48758ed8fea36c8ce0e752a45) )	/* sprite color ROM (upper 4 bits) */

	ROM_REGION( 0x0100, "namco", 0 ) /* sound prom */
	ROM_LOAD( "gp2-4.3f",     0x0000, 0x0100, CRC(2d9fbdd8) SHA1(e6a23cd5ce3d3e76de3b70c8ab5a3c45b1147af4) )
ROM_END

ROM_START( galaga3a )
	ROM_REGION( 0x10000, "maincpu", 0 ) /* 64k for the MAIN CPU */
	ROM_LOAD( "gal3_9e.bin",  0xa000, 0x2000, CRC(f4845e7f) SHA1(7b1377254f594bea4a8ffc7e388d9106e0266b55) )
	ROM_LOAD( "gal3_9d.bin",  0xc000, 0x2000, CRC(86fac687) SHA1(07f76af524dbb3e79de41ef4bf32e7380776d9f5) )
	ROM_LOAD( "gal3_9c.bin",  0xe000, 0x2000, CRC(f1b00073) SHA1(5d998d938251f173cedf742b95d02cc0a2b9d3be) )

	ROM_REGION( 0x10000, "sub", 0 ) /* 64k for the SUB CPU */
	ROM_LOAD( "gal3_6l.bin",  0xa000, 0x2000, CRC(9ec3dce5) SHA1(196a975aff59be19f55041a44b201aafef083ba7) )
	ROM_LOAD( "gp2-7.11c",    0xc000, 0x2000, CRC(0621f7df) SHA1(b86020f819fefb134cb57e203f7c90b1b29581c8) )
	ROM_LOAD( "gal3_6n.bin",  0xe000, 0x2000, CRC(6a2942c5) SHA1(6fb2c4dcb2ad393220917b81f1a42e571d209d76) )

	ROM_REGION( 0x10000, "sub2", 0 ) /* 64k for the SOUND CPU */
	ROM_LOAD( "gp2-1.4b",     0xe000, 0x2000, CRC(ed8aa206) SHA1(4e0a31d84cb7aca497485dbe0240009d58275765) )

	ROM_REGION( 0x4000, "gfx1", ROMREGION_DISPOSE )
	ROM_LOAD( "gal3_9l.bin",  0x0000, 0x2000, CRC(8d4dcebf) SHA1(0a556b45976bc36eb99048b1512c446b472da1d2) )	/* characters */
	/* 0x2000-0x3fff  will be unpacked from 0x0000-0x1fff */

	ROM_REGION( 0xc000, "gfx2", ROMREGION_DISPOSE )
	ROM_LOAD( "gp2-11.11p",   0x0000, 0x2000, CRC(57740ff9) SHA1(16873e0ac5f975768d596d7d32af7571f4817f2b) )	/* objects */
	ROM_LOAD( "gp2-10.11n",   0x2000, 0x2000, CRC(6cd8ce11) SHA1(fc346e98737c9fc20810e32d4c150ae4b4051979) )	/* objects */
	ROM_LOAD( "gp2-12.11r",   0x4000, 0x2000, CRC(7316a1f1) SHA1(368e4541a5151e906a189712bc05192c2ceec8ae) )	/* objects */
	ROM_LOAD( "gp2-9.11m",    0x6000, 0x2000, CRC(e6a9ae67) SHA1(99c1e67c3b216aa1b63f199e21c73cdedde80e1b) )	/* objects */
	/* 0x8000-0x9fff  will be unpacked from 0x6000-0x7fff */
	ROM_FILL(                 0xa000, 0x2000, 0x00 )	// optional ROM, not used

	ROM_REGION( 0x0800, "proms", 0 )
	ROM_LOAD( "gp2-3.1p",     0x0000, 0x0100, CRC(a5091352) SHA1(dcd6dfbfbd5281ba0c7b7c189d6fde23617ed3e3) )	/* red palette ROM (4 bits) */
	ROM_LOAD( "gp2-1.1n",     0x0100, 0x0100, CRC(8bc8022a) SHA1(c76f9d9b066e268621d41a703c5280261234709a) )	/* green palette ROM (4 bits) */
	ROM_LOAD( "gp2-2.2n",     0x0200, 0x0100, CRC(8dabc20b) SHA1(64d7b333f529d3ba66aeefd380fd1cbf9ddf460d) )	/* blue palette ROM (4 bits) */
	ROM_LOAD( "gp2-7.6s",     0x0300, 0x0100, CRC(2faa3e09) SHA1(781ffe9088476798409cb922350eff881590cf35) )	/* char color ROM */
	ROM_LOAD( "g3_3f.bin",    0x0400, 0x0200, CRC(d48c0eef) SHA1(6d0512958bc522d22e69336677369507847f8f6f) )	/* sprite color ROM (lower 4 bits) */
	ROM_LOAD( "g3_3e.bin",    0x0600, 0x0200, CRC(417ba0dc) SHA1(2ba51ccdd0428fc48758ed8fea36c8ce0e752a45) )	/* sprite color ROM (upper 4 bits) */

	ROM_REGION( 0x0100, "namco", 0 ) /* sound prom */
	ROM_LOAD( "gp2-4.3f",     0x0000, 0x0100, CRC(2d9fbdd8) SHA1(e6a23cd5ce3d3e76de3b70c8ab5a3c45b1147af4) )
ROM_END

ROM_START( galaga3m )
	ROM_REGION( 0x10000, "maincpu", 0 ) /* 64k for the MAIN CPU */
	ROM_LOAD( "m1.9e",        0xa000, 0x2000, CRC(e392704e) SHA1(8eebd48dfe8491f491e844d4ad0964e25efb013b) )
	ROM_LOAD( "gal3_9d.bin",  0xc000, 0x2000, CRC(86fac687) SHA1(07f76af524dbb3e79de41ef4bf32e7380776d9f5) )
	ROM_LOAD( "gal3_9c.bin",  0xe000, 0x2000, CRC(f1b00073) SHA1(5d998d938251f173cedf742b95d02cc0a2b9d3be) )

	ROM_REGION( 0x10000, "sub", 0 ) /* 64k for the SUB CPU */
	ROM_LOAD( "gal3_6l.bin",  0xa000, 0x2000, CRC(9ec3dce5) SHA1(196a975aff59be19f55041a44b201aafef083ba7) )
	ROM_LOAD( "gp2-7.11c",    0xc000, 0x2000, CRC(0621f7df) SHA1(b86020f819fefb134cb57e203f7c90b1b29581c8) )
	ROM_LOAD( "gal3_6n.bin",  0xe000, 0x2000, CRC(6a2942c5) SHA1(6fb2c4dcb2ad393220917b81f1a42e571d209d76) )

	ROM_REGION( 0x10000, "sub2", 0 ) /* 64k for the SOUND CPU */
	ROM_LOAD( "gp2-1.4b",     0xe000, 0x2000, CRC(ed8aa206) SHA1(4e0a31d84cb7aca497485dbe0240009d58275765) )

	ROM_REGION( 0x4000, "gfx1", ROMREGION_DISPOSE )
	ROM_LOAD( "gal3_9l.bin",  0x0000, 0x2000, CRC(8d4dcebf) SHA1(0a556b45976bc36eb99048b1512c446b472da1d2) )	/* characters */
	/* 0x2000-0x3fff  will be unpacked from 0x0000-0x1fff */

	ROM_REGION( 0xc000, "gfx2", ROMREGION_DISPOSE )
	ROM_LOAD( "gp2-11.11p",   0x0000, 0x2000, CRC(57740ff9) SHA1(16873e0ac5f975768d596d7d32af7571f4817f2b) )	/* objects */
	ROM_LOAD( "gp2-10.11n",   0x2000, 0x2000, CRC(6cd8ce11) SHA1(fc346e98737c9fc20810e32d4c150ae4b4051979) )	/* objects */
	ROM_LOAD( "gp2-12.11r",   0x4000, 0x2000, CRC(7316a1f1) SHA1(368e4541a5151e906a189712bc05192c2ceec8ae) )	/* objects */
	ROM_LOAD( "gp2-9.11m",    0x6000, 0x2000, CRC(e6a9ae67) SHA1(99c1e67c3b216aa1b63f199e21c73cdedde80e1b) )	/* objects */
	/* 0x8000-0x9fff  will be unpacked from 0x6000-0x7fff */
	ROM_FILL(                 0xa000, 0x2000, 0x00 )	// optional ROM, not used

	ROM_REGION( 0x0800, "proms", 0 )
	ROM_LOAD( "gp2-3.1p",     0x0000, 0x0100, CRC(a5091352) SHA1(dcd6dfbfbd5281ba0c7b7c189d6fde23617ed3e3) )	/* red palette ROM (4 bits) */
	ROM_LOAD( "gp2-1.1n",     0x0100, 0x0100, CRC(8bc8022a) SHA1(c76f9d9b066e268621d41a703c5280261234709a) )	/* green palette ROM (4 bits) */
	ROM_LOAD( "gp2-2.2n",     0x0200, 0x0100, CRC(8dabc20b) SHA1(64d7b333f529d3ba66aeefd380fd1cbf9ddf460d) )	/* blue palette ROM (4 bits) */
	ROM_LOAD( "gp2-7.6s",     0x0300, 0x0100, CRC(2faa3e09) SHA1(781ffe9088476798409cb922350eff881590cf35) )	/* char color ROM */
	ROM_LOAD( "g3_3f.bin",    0x0400, 0x0200, CRC(d48c0eef) SHA1(6d0512958bc522d22e69336677369507847f8f6f) )	/* sprite color ROM (lower 4 bits) */
	ROM_LOAD( "g3_3e.bin",    0x0600, 0x0200, CRC(417ba0dc) SHA1(2ba51ccdd0428fc48758ed8fea36c8ce0e752a45) )	/* sprite color ROM (upper 4 bits) */

	ROM_REGION( 0x0100, "namco", 0 ) /* sound prom */
	ROM_LOAD( "gp2-4.3f",     0x0000, 0x0100, CRC(2d9fbdd8) SHA1(e6a23cd5ce3d3e76de3b70c8ab5a3c45b1147af4) )
ROM_END


static void unpack_gfx(running_machine *machine)
{
	UINT8 *rom;
	int i;

	rom = memory_region(machine, "gfx1");
	for (i = 0;i < 0x2000;i++)
		rom[i + 0x2000] = rom[i] >> 4;

	rom = memory_region(machine, "gfx2") + 0x6000;
	for (i = 0;i < 0x2000;i++)
		rom[i + 0x2000] = rom[i] << 4;
}


GAME( 1984, gaplus,   0,        gaplus,   gaplus,   56_58l, ROT90, "Namco", "Gaplus (rev. D)", GAME_IMPERFECT_GRAPHICS )
GAME( 1984, galaga3,  gaplus,   gaplus,   gaplus,   56_58l, ROT90, "Namco", "Galaga 3 (rev. C)", GAME_IMPERFECT_GRAPHICS )
GAME( 1984, gapluso,  gaplus,   gaplus,   gapluso,  56_58,  ROT90, "Namco", "Gaplus (rev. B)", GAME_IMPERFECT_GRAPHICS )
GAME( 1984, gaplusa,  gaplus,   gaplus,   gapluso,  58_56,  ROT90, "Namco", "Gaplus (alternate hardware)", GAME_IMPERFECT_GRAPHICS )
GAME( 1984, galaga3a, gaplus,   gaplus,   galaga3a, 56_58l, ROT90, "Namco", "Galaga 3 (set 2)", GAME_IMPERFECT_GRAPHICS )
GAME( 1984, galaga3m, gaplus,   gaplus,   galaga3m, 56_58l, ROT90, "Namco", "Galaga 3 (set 3)", GAME_IMPERFECT_GRAPHICS )
