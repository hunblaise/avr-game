#include "avr/io.h"
#include "lcd.h"

#define __AVR_ATMEGA128__ 1



// GENERAL INIT - USED BY ALMOST EVERYTHING ----------------------------------

static void port_init() {
    PORTA = 0b00000000; DDRA = 0b01000000; // buttons
    PORTB = 0b00000000; DDRB = 0b00000000;
    PORTC = 0b00000000; DDRC = 0b11110111; // lcd
    PORTD = 0b11000000; DDRD = 0b00001000;
    PORTE = 0b00100000; DDRE = 0b00110000; // buzzer
    PORTF = 0b00000000; DDRF = 0b00000000;
    PORTG = 0b00000000; DDRG = 0b00000000;
}

// TIMER-BASED RANDOM NUMBER GENERATOR ---------------------------------------

static void rnd_init() {
    TCCR0 |= (1 << CS00); // Timer 0 no prescaling (@FCPU)
    TCNT0 = 0; // init counter
}

// generate a value between 0 and max
static int rnd_gen(int max) {
    return TCNT0 % max;
}

// SOUND GENERATOR -----------------------------------------------------------

typedef struct {
    int freq;
    int length;
} tune_t;

static tune_t TUNE_START[] = { { 2000, 40 }, { 0, 0 } };
static tune_t TUNE_LEVELUP[] = { { 3000, 20 }, { 0, 0 } };
static tune_t TUNE_GAMEOVER[] = { { 1000, 200 }, { 1500, 200 }, { 2000, 400 }, { 0, 0 } };

static void play_note(int freq, int len) {
    for (int l = 0; l < len; ++l) {
        int i;
        PORTE = (PORTE & 0b11011111) | 0b00010000; //set bit4 = 1; set bit5 = 0
        for (i = freq; i; i--);
        PORTE = (PORTE | 0b00100000) & 0b11101111; //set bit4 = 0; set bit5 = 1
        for (i = freq; i; i--);
    }
}

static void play_tune(tune_t *tune) {
    while (tune->freq != 0) {
        play_note(tune->freq, tune->length);
        ++tune;
    }
}

// BUTTON HANDLING -----------------------------------------------------------

#define BUTTON_NONE 0
#define BUTTON_CENTER 1
#define BUTTON_LEFT 2
#define BUTTON_RIGHT 3
#define BUTTON_UP 4
static int button_accept = 1;

static int button_pressed() {
// right
    if (!(PINA & 0b00000001) & button_accept) { // check state of button 1 and value of button_accept
        button_accept = 0; // button is pressed
        return BUTTON_RIGHT;
    }

// up
    if (!(PINA & 0b00000010) & button_accept) { // check state of button 2 and value of button_accept
        button_accept = 0; // button is pressed
        return BUTTON_UP;
    }

// center
    if (!(PINA & 0b00000100) & button_accept) { // check state of button 2 and value of button_accept
        button_accept = 0; // button is pressed
        return BUTTON_CENTER;
    }

// left
    if (!(PINA & 0b00010000) & button_accept) { // check state of button 5 and value of button_accept
        button_accept = 0; // button is pressed
        return BUTTON_LEFT;
    }

    return BUTTON_NONE;
}

static void button_unlock() {
//check state of all buttons
    if (
        ((PINA & 0b00000001)
         | (PINA & 0b00000010)
         | (PINA & 0b00000100)
         | (PINA & 0b00001000)
         | (PINA & 0b00010000)) == 31)
        button_accept = 1; //if all buttons are released button_accept gets value 1
}

// LCD HELPERS ---------------------------------------------------------------

static void lcd_delay(unsigned int a) {
    while (a)
        a--;
}

static void lcd_pulse() {
    PORTC = PORTC | 0b00000100; //set E to high
    lcd_delay(1400); //delay ~110ms
    PORTC = PORTC & 0b11111011; //set E to low
}

static void lcd_send(int command, unsigned char a) {
    unsigned char data;

    data = 0b00001111 | a; //get high 4 bits
    PORTC = (PORTC | 0b11110000) & data; //set D4-D7
    if (command)
        PORTC = PORTC & 0b11111110; //set RS port to 0 -> display set to command mode
    else
        PORTC = PORTC | 0b00000001; //set RS port to 1 -> display set to data mode
    lcd_pulse(); //pulse to set D4-D7 bits

    data = a << 4; //get low 4 bits
    PORTC = (PORTC & 0b00001111) | data; //set D4-D7
    if (command)
        PORTC = PORTC & 0b11111110; //set RS port to 0 -> display set to command mode
    else
        PORTC = PORTC | 0b00000001; //set RS port to 1 -> display set to data mode
    lcd_pulse(); //pulse to set d4-d7 bits
}

static void lcd_send_command(unsigned char a) {
    lcd_send(1, a);
}

static void lcd_send_data(unsigned char a) {
    lcd_send(0, a);
}

static void lcd_init() {
//LCD initialization
//step by step (from Gosho) - from DATASHEET

    PORTC = PORTC & 0b11111110;

    lcd_delay(10000);

    PORTC = 0b00110000; //set D4, D5 port to 1
    lcd_pulse(); //high->low to E port (pulse)
    lcd_delay(1000);

    PORTC = 0b00110000; //set D4, D5 port to 1
    lcd_pulse(); //high->low to E port (pulse)
    lcd_delay(1000);

    PORTC = 0b00110000; //set D4, D5 port to 1
    lcd_pulse(); //high->low to E port (pulse)
    lcd_delay(1000);

    PORTC = 0b00100000; //set D4 to 0, D5 port to 1
    lcd_pulse(); //high->low to E port (pulse)

    lcd_send_command(DISP_ON); // Turn ON Display
    lcd_send_command(CLR_DISP); // Clear Display
}

static void lcd_send_text(char *str) {
    while (*str)
        lcd_send_data(*str++);
}

static void lcd_send_line1(char *str) {
    lcd_send_command(DD_RAM_ADDR);
    lcd_send_text(str);
}

static void lcd_send_line2(char *str) {
    lcd_send_command(DD_RAM_ADDR2);
    lcd_send_text(str);
}

// SPEED LEVELS --------------------------------------------------------------

typedef struct {
    int delay;
    int rows;
} level_t;

#define LEVEL_NUM 6
static level_t LEVELS[] = { { 5, 5 }, { 4, 10 }, { 3, 15 }, { 2, 20 }, { 1, 30 }, { 0, 0 } };
static int level_current = 0;
static int delay_cycle;

static void row_removed() {
// do nothing if already at top speed
    if (level_current == LEVEL_NUM - 1)
        return;

// if enough rows removed, increase speed
    if (--LEVELS[level_current].rows == 0) {
        ++level_current;
        play_tune(TUNE_LEVELUP);
    }
}

// PATTERNS AND PLAYFIELD ----------------------------------------------------
/* Vertical axis: 0 is top row, increments downwards
* Horizontal axis: 0 is right column, increments leftwards */

#define PATTERN_NUM 4
#define PATTERN_SIZE 2
static unsigned char PATTERNS[PATTERN_NUM][PATTERN_SIZE] = { { 0b01, 0b00 }, { 0b01, 0b01 }, { 0b01, 0b11 }, { 0b11, 0b11 } };

static unsigned char current_pattern[PATTERN_SIZE];
static int current_row;
static int current_col;

// Actually, 1 row taller and 2 columns wider, which extras are filled with ones to help collision detection
#define PLAYFIELD_ROWS 16
#define PLAYFIELD_COLS 4
static unsigned char playfield[PLAYFIELD_ROWS + 1];

static void playfield_clear() {
    for (int r = 0; r < PLAYFIELD_ROWS; ++r)
        playfield[r] = 0b100001;
    playfield[PLAYFIELD_ROWS] = 0b111111;
}

static void merge_current_pattern_to_playfield() {
// merge current piece to playfield
    for (int p = 0; p < PATTERN_SIZE; ++p)
        playfield[current_row + p] |= current_pattern[p] << (current_col + 1);
// remove full lines and drop lines above
    for (int r = 0; r < PLAYFIELD_ROWS; ++r) {
        if (playfield[r] == 0b111111) {
            for (int rr = r; rr > 0; --rr)
                playfield[rr] = playfield[rr - 1];
            playfield[0] = 0b100001;
            row_removed(); // let's see whether we should increase the speed
        }
    }
}

static int collision(char *pattern, int row, int col) {
    int result = 0;
    for (int r = 0; r < PATTERN_SIZE; ++r)
        result |= playfield[row + r] & (pattern[r] << (col + 1));
    return !!result;
}

static void rotate_pattern(char *src_pattern, char *dst_pattern) {
// rotate the piece
    dst_pattern[0] = (src_pattern[0] >> 1) | ((src_pattern[1] >> 1) << 1);
    dst_pattern[1] = (src_pattern[0] & 0x01) | ((src_pattern[1] & 0x01) << 1);
// if the topmost row of the rotated piece is empty, shift the pattern upwards
    if (dst_pattern[0] == 0) {
        dst_pattern[0] = dst_pattern[1];
        dst_pattern[1] = 0;
    }
// if the rightmost column of the rotated piece is empty, shift the pattern to the right
    if (((dst_pattern[0] & 0b01) == 0) && ((dst_pattern[1] & 0b01) == 0)) {
        dst_pattern[0] >>= 1;
        dst_pattern[1] >>= 1;
    }
}

// GRAPHICS ------------------------------------------------------------------

#define CHAR_EMPTY_PATTERN 0
#define CHAR_EMPTY_PLAYGROUND 1
#define CHAR_PATTERN_EMPTY 2
#define CHAR_PATTERN_PATTERN 3
#define CHAR_PATTERN_PLAYGROUND 4
#define CHAR_PLAYGROUND_EMPTY 5
#define CHAR_PLAYGROUND_PATTERN 6
#define CHAR_PLAYGROUND_PLAYGROUND 7
#define CHAR_EMPTY_EMPTY ' '
#define CHAR_ERROR 'X'

#define CHARMAP_SIZE 8
static unsigned char CHARMAP[CHARMAP_SIZE][8] = {
    { 0b10101, 0b01010, 0b10101, 0b01010, 0, 0, 0, 0 }, // CHAR_EMPTY_PATTERN
    { 0b11111, 0b11111, 0b11111, 0b11111, 0, 0, 0, 0 }, // CHAR_EMPTY_PLAYGROUND
    { 0, 0, 0, 0, 0b10101, 0b01010, 0b10101, 0b01010 }, // CHAR_PATTERN_EMPTY
    { 0b10101, 0b01010, 0b10101, 0b01010, 0b10101, 0b01010, 0b10101, 0b01010 }, // CHAR_PATTERN_PATTERN
    { 0b11111, 0b11111, 0b11111, 0b11111, 0b10101, 0b01010, 0b10101, 0b01010 }, // CHAR_PATTERN_PLAYGROUND
    { 0, 0, 0, 0, 0b11111, 0b11111, 0b11111, 0b11111 }, // CHAR_PLAYGROUND_EMPTY
    { 0b10101, 0b01010, 0b10101, 0b01010, 0b11111, 0b11111, 0b11111, 0b11111 }, // CHAR_PLAYGROUND_PATTERN
    { 0b11111, 0b11111, 0b11111, 0b11111, 0b11111, 0b11111, 0b11111, 0b11111 } // CHAR_PLAYGROUND_PLAYGROUND
};

static const unsigned char XLAT_PATTERN[] = { 0b0000, 0b0001, 0b0100, 0b0101 };
static const unsigned char XLAT_PLAYGROUND[] = { 0b0000, 0b0010, 0b1000, 0b1010 };
static const char XLAT_CHAR[] = {
    CHAR_EMPTY_EMPTY, // 0b0000
    CHAR_EMPTY_PATTERN, // 0b0001
    CHAR_EMPTY_PLAYGROUND, // 0b0010
    CHAR_ERROR, // 0b0011
    CHAR_PATTERN_EMPTY, // 0b0100
    CHAR_PATTERN_PATTERN, // 0b0101
    CHAR_PATTERN_PLAYGROUND, // 0b0110
    CHAR_ERROR, // 0b0111
    CHAR_PLAYGROUND_EMPTY, // 0b1000
    CHAR_PLAYGROUND_PATTERN, // 0b1001
    CHAR_PLAYGROUND_PLAYGROUND, // 0b1010
    CHAR_ERROR, // 0b1011
    CHAR_ERROR, // 0b1100
    CHAR_ERROR, // 0b1101
    CHAR_ERROR, // 0b1110
    CHAR_ERROR // 0b1111
};

#define CG_RAM_ADDR 0x40

static void chars_init() {
    for (int c = 0; c < CHARMAP_SIZE; ++c) {
        lcd_send_command(CG_RAM_ADDR + c * 8);
        for (int r = 0; r < 8; ++r)
            lcd_send_data(CHARMAP[c][r]);
    }
}

static void screen_update() {
    lcd_send_command(DD_RAM_ADDR); //set to Line 1

    for (int r1 = 0; r1 < PLAYFIELD_ROWS; ++r1) {
        unsigned char row = XLAT_PLAYGROUND[(playfield[r1] >> 1) & 0b11];
        for (int pr = 0; pr < PATTERN_SIZE; ++pr)
            if (r1 == current_row + pr)
                row |= XLAT_PATTERN[(current_pattern[pr] << current_col) & 0b11];
        lcd_send_data(XLAT_CHAR[row]);
    }

    lcd_send_command(DD_RAM_ADDR2); //set to Line 2

    for (int r2 = 0; r2 < PLAYFIELD_ROWS; ++r2) {
        char row = XLAT_PLAYGROUND[(playfield[r2] >> 3) & 0b11];
        for (int pr = 0; pr < PATTERN_SIZE; ++pr)
            if (r2 == current_row + pr)
                row |= XLAT_PATTERN[((current_pattern[pr] << current_col) >> 2) & 0b11];
        lcd_send_data(XLAT_CHAR[row]);
    }
}

// THE GAME ==================================================================

// PATTERNS

static const unsigned char BOX_UP_FULL[8] =
{   0b11111,
    0b11111,

    0b11111,
    0b11111,

    0b11111,
    0b11111,

    0b00000,
    0b00000
};

static const unsigned char BOX_UP_FIRST_HALF[8] =
{   0b11100,
    0b11100,

    0b11100,
    0b11100,

    0b11100,
    0b11100,

    0b00000,
    0b00000
};

static const unsigned char BOX_UP_SECOND_HALF[8] =
{   0b00111,
    0b00111,

    0b00111,
    0b00111,

    0b00111,
    0b00111,

    0b00000,
    0b00000
};


static const unsigned char BOX_DOWN_FULL[8] =
{   0b00000,
    0b00000,

    0b11111,
    0b11111,

    0b11111,
    0b11111,

    0b11111,
    0b11111
};

static const unsigned char BOX_DOWN_FIRST_HALF[8] =
{   0b00000,
    0b00000,

    0b11100,
    0b11100,

    0b11100,
    0b11100,

    0b11100,
    0b11100
};

static const unsigned char BOX_DOWN_SECOND_HALF[8] =
{   0b00000,
    0b00000,

    0b11100,
    0b11100,

    0b00111,
    0b00111,

    0b00111,
    0b00111
};

static const unsigned char PLAYER_PATTERN[8] =
{   0b00000,
    0b00000,

    0b01110,
    0b01110,

    0b01110,
    0b01110,

    0b00000,
    0b00000
};

static const unsigned char EMPTY_PATTERN[8] =
{   0b00000,
    0b00000,

    0b00000,
    0b00000,

    0b00000,
    0b00000,

    0b00000,
    0b00000
};

static void pattern_init()
{
    lcd_send_command(CG_RAM_ADDR);
    for (int i = 0; i < 8; ++i)
    {
        lcd_send_data(BOX_UP_FULL[i]);
    }

    for (int i = 0; i < 8; ++i)
    {
        lcd_send_data(BOX_UP_FIRST_HALF[i]);
    }

    for (int i = 0; i < 8; ++i)
    {
        lcd_send_data(BOX_UP_SECOND_HALF[i]);
    }

    for (int i = 0; i < 8; ++i)
    {
        lcd_send_data(BOX_DOWN_FULL[i]);
    }

    for (int i = 0; i < 8; ++i)
    {
        lcd_send_data(BOX_DOWN_FIRST_HALF[i]);
    }

    for (int i = 0; i < 8; ++i)
    {
        lcd_send_data(BOX_DOWN_SECOND_HALF[i]);
    }

    for (int i = 0; i < 8; ++i)
    {
        lcd_send_data(PLAYER_PATTERN[i]);
    }

    for (int i = 0; i < 8; ++i)
    {
        lcd_send_data(EMPTY_PATTERN[i]);
    }
}

// BARRIER

#define EMPTY_SPOT 0
#define BOX_SPOT 1
#define PLAYER_SPOT 2

static int LEFT_ROW[16] = { EMPTY_SPOT };
static int RIGHT_ROW[16] = { EMPTY_SPOT };

static void generate_barrier() {
    int rnd = rnd_gen(10);

    if (rnd > 2)
    {
        if (rnd > 5 && RIGHT_ROW[14] != BOX_SPOT)
        {
            LEFT_ROW[15] = BOX_SPOT;
        } else if (LEFT_ROW[14] != BOX_SPOT)
        {
            RIGHT_ROW[15] = BOX_SPOT;
        }
    }
}

static void iterate_barriers()
{
    for (int i = 0; i < 15; ++i)
    {
        if (LEFT_ROW[i + 1] == BOX_SPOT && LEFT_ROW[i] != PLAYER_SPOT)
        {
            LEFT_ROW[i] = BOX_SPOT;
            LEFT_ROW[i + 1] = EMPTY_SPOT;
        }

        if (RIGHT_ROW[i + 1] == BOX_SPOT && RIGHT_ROW[i] != PLAYER_SPOT)
        {
            RIGHT_ROW[i] = BOX_SPOT;
            RIGHT_ROW[i + 1] = EMPTY_SPOT;
        }
    }

    if (LEFT_ROW[0] == BOX_SPOT)
    {
        LEFT_ROW[0] = EMPTY_SPOT;
    }
    if (RIGHT_ROW[0] == BOX_SPOT)
    {
        RIGHT_ROW[0] = EMPTY_SPOT;
    }
}

#define UP 0
#define UP_FIRST_HALF 1
#define UP_SECOND_HALF 2
#define DOWN 3
#define DOWN_FIRST_HALF 4
#define DOWN_SECOND_HALF 5
#define PLAYER 6
#define EMPTY 7

// BARRIER MOVEMENT

static void display_playfield()
{
    for (int i = 0; i < 16; ++i)
    {
        if (LEFT_ROW[i] == PLAYER_SPOT)
        {
            lcd_send_command(DD_RAM_ADDR2 + i);
            lcd_send_data(PLAYER);
        } else if (LEFT_ROW[i] == BOX_SPOT)
        {
            lcd_send_command(DD_RAM_ADDR2 + i);
            lcd_send_data(UP);
        } else if (LEFT_ROW[i] == EMPTY_SPOT)
        {
            lcd_send_command(DD_RAM_ADDR2 + i);
            lcd_send_data(EMPTY);
        } else
        {
            lcd_send_command(DD_RAM_ADDR2 + i);
            lcd_send_data(EMPTY);
        }

        if (RIGHT_ROW[i] == PLAYER_SPOT)
        {
            lcd_send_command(DD_RAM_ADDR + i);
            lcd_send_data(PLAYER);
        } else if (RIGHT_ROW[i] == BOX_SPOT)
        {
            lcd_send_command(DD_RAM_ADDR + i);
            lcd_send_data(DOWN);
        } else if (RIGHT_ROW[i] == EMPTY_SPOT)
        {
            lcd_send_command(DD_RAM_ADDR + i);
            lcd_send_data(EMPTY);
        } else
        {
            lcd_send_command(DD_RAM_ADDR + i);
            lcd_send_data(EMPTY);
        }
    }
}

// PLAYER MOVEMENT

static void step_left()
{
    LEFT_ROW[2] = PLAYER_SPOT;
    RIGHT_ROW[2] = EMPTY_SPOT;
}

static void step_right()
{
    RIGHT_ROW[2] = PLAYER_SPOT;
    LEFT_ROW[2] = EMPTY_SPOT;
}

static void player_init()
{
    LEFT_ROW[2] = PLAYER_SPOT;
}

int main() {
    port_init();
    lcd_init();
    pattern_init();
    player_init();
    rnd_init();

    while (1) {
        generate_barrier();
        display_playfield();

        int button = button_pressed();
        if (button == BUTTON_LEFT)
        {
            step_left();
        }
        if (button == BUTTON_RIGHT)
        {
            step_right();
        }
        if (button == BUTTON_UP)
        {
            iterate_barriers();
            generate_barrier();

        }
        if (button == BUTTON_CENTER)
        {
            display_playfield();
        }
        button_unlock();

        lcd_delay(1300000);
        iterate_barriers();
    }
}
