
#include <stdlib.h>
#include <stdio.h>
#include "platform.h"
#include <xiicps.h>
#include <xil_cache.h>
#include <xil_exception.h>
#include <xgpio.h>
#include <xgpiops.h>
#include <xscuwdt.h>
#include <sleep.h>
#include <xtime_l.h>
#include "font_5x7.h"
#include "Inspire.h"
#include <stdint.h>

static const u8 oled_addr = 0x3c;
static const int oled_reset_pin = 9;
static const int i2c_write_delay = 300; // us

// Write a command out to the specified i2c device.
void i2c_command(XIicPs *device, u8 addr, u8 command)
{
    const u8 buf[] = {0x80,command};
    XIicPs_MasterSend(device, (u8*)buf, sizeof(buf), addr);
    usleep(i2c_write_delay);
}

// Write data out to the specified i2c device.
void i2c_data(XIicPs *device, u8 addr, u8 data)
{
    const u8 buf[] = {0x40,data};
    XIicPs_MasterSend(device, (u8*)buf, sizeof(buf), addr);
    usleep(i2c_write_delay);
}

// A font descriptor.
typedef struct {
    const u8 * base;
    int size;
    char start;
    int width;
    int pad;
} font_t;

// Tailor the font to the display.
const font_t font = {
    .base = Font5x7,
    .size = sizeof(Font5x7),
    .start = ' ',
    .width = 5,
    .pad = 1//28%5
};

// Display a character on the device.
// Map the character onto the font index, and then write the font data out.
void display_character(XIicPs *device, u8 addr, char c)
{
    int index = (font.width * (c - font.start));
    if ((index < 0) || (index >= font.size)) {

        // default to '?' if unknown.
        // This isn't optimal, but it's good enough for now.
        c = '?';
        index = (font.width * (c - font.start));
    }

    int i;
    for (i = 0; i < font.width; ++i) {
        i2c_data(device, addr, font.base[index+i]);
    }
    for (i = 0; i < font.pad; ++i) {
        i2c_data(device, addr, 0);
    }
    putchar(c);
}

// An i2c device context.
typedef struct {
    int id;
    int clock;
    int clear_options;
    int options;

    XIicPs_Config *config;
    XIicPs device;
} i2c_t;

// Set Page Start: 1 0 1 1 0 X2 X1 X0
static void ssd1306_set_page_start(i2c_t *i2c, u8 page)
{
    u8 buf[2];
    buf [0]= 0x80;
    buf [1]= 0xb0|(page&0x7);
    XIicPs_MasterSend(&i2c->device, buf, 2, oled_addr);
    usleep(i2c_write_delay);
    ssd1306_set_col_start(i2c, 0);
}

void ssd1306_set_col_start(i2c_t *i2c, u8 col)
{
    u8 buf[2];
    buf [0]= 0x80;
    buf [1]= 0x00+col%16;
    XIicPs_MasterSend(&i2c->device, buf, 2, oled_addr);
    usleep(i2c_write_delay);
    buf [1]= 0x10+col/16;
    XIicPs_MasterSend(&i2c->device, buf, 2, oled_addr);

    usleep(i2c_write_delay);
}

// Clear a line and return the cursor to the beginning of the line.
static void ssd1306_clear_line(i2c_t *i2c, u8 line)
{
    ssd1306_set_page_start(i2c, line);
    int i;
    for (i = 0; i < 128; ++i) {
        i2c_data(&i2c->device, oled_addr, 0);
    }
    ssd1306_set_page_start(i2c, line);
    putchar('\n');
}

// Display a word to the display, with the following semantics: Words may only
// break across lines if 3+ characters can first be rendered; otherwise, pad
// out the current line. This assumes we can support 16 characters per line,
// and that there are 8 lines on the display.
// @todo there's probably a better way to do this.
// @note This emulates the display output on the console for debugging purposes.
void display_word(i2c_t *i2c, u8 addr, char *w, int n)
{
    int i;
    static u8 x = 0, y = 0;

    // If the word is multiline, check for wrapping/padding, and make it so.
    static const int max_chars = 16;
    static const int max_lines = 8;
    const int remaining = max_chars - x;

    if ((n > remaining) && (remaining < 3)) {
        x = 0;
        y = (y+1)%max_lines;
        for (i = 0; i < remaining; ++i) {
            display_character(&i2c->device, addr, ' ');
        }
        ssd1306_clear_line(i2c, y);
    }

    // Keep track of line breaks.
    for (i = 0; i < n; ++i) {

        // If this is the beginning of the line, eat whitespace.
        if (!x && w[i] == ' ') {
            continue;
        }

        // Otherwise, display the character. Blank line wraps to make things
        // easier on the reader.
        display_character(&i2c->device, addr, w[i]);
        x=(x+1)%max_chars;
        if (!x) {
            y=(y+1)%max_lines;
            ssd1306_clear_line(i2c, y);
        }
    }
}

void display_string(i2c_t *i2c, u8 addr, char w[]) {
	int i = 0;
	do{
		display_character(&i2c->device, addr, w[i]);
		i++;
	}while (w[i] != '\0');
}

void print_error(i2c_t *i2c, u8 addr, char w[]) {
	ssd1306_clear_line(i2c, 7);
	display_string(i2c, addr, w);
}

// Initialize an i2c device.
static int initialize_i2c(i2c_t *i2c)
{
    i2c->config = XIicPs_LookupConfig(i2c->id); //XPAR_PS7_I2C_0_DEVICE_ID);
    if (!i2c->config) {
        printf("XIicPs_LookupConfig failed\n");
        return XST_FAILURE;
    }
    printf("i2c initialization\n");
    int status = XIicPs_CfgInitialize(&i2c->device, i2c->config,
            i2c->config->BaseAddress);
    if (status != XST_SUCCESS) {
        printf("XIicPs_CfgInitialize failed %d\n", status);
        return status;
    }
    status = XIicPs_SelfTest(&i2c->device);
    if (status != XST_SUCCESS) {
        printf("XIicPs_SelfTest failed %d\n", status);
        return status;
    }
    status = XIicPs_SetSClk(&i2c->device, i2c->clock); //100000);
    if (status != XST_SUCCESS) {
        printf("XIicPs_SetSClk failed %d\n", status);
        return status;
    }
    status = XIicPs_ClearOptions(&i2c->device, i2c->clear_options);//XIICPS_10_BIT_ADDR_OPTION);
    if (status != XST_SUCCESS) {
        printf("XIicPs_ClearOptions failed %d\n", status);
        return status;
    }
    status = XIicPs_SetOptions(&i2c->device, i2c->options);//XIICPS_7_BIT_ADDR_OPTION);
    if (status != XST_SUCCESS) {
        printf("XIicPs_SetOptions failed %d\n", status);
        return status;
    }
    return 0;
}



// Set the ttc frequency specified in the driver context.
// Note that the calculation can fail: Upon unsuccessful calculation, Interval
// and Prescaler are set to 0xFF(FF) for their maximum values to signal the
// caller of failure.

// Issue the set display start line command.
// This can be used to support scrolling.
static void ssd1306_set_display_start_line(i2c_t *i2c, int line)
{
	//const u8 buf[] = {0x80,0xb0|(line&0x7)};
    const u8 buf[] = {0x80,1<<6|line};
    XIicPs_MasterSend(&i2c->device, (u8*)buf, sizeof(buf), oled_addr);
    usleep(i2c_write_delay);
}


// Initialize internal gpio.
// This is used to control the mio7 led.
static int initialize_gpio(XGpioPs *gpio)
{
    XGpioPs_Config *gpio_config =
        XGpioPs_LookupConfig(XPAR_PS7_GPIO_0_DEVICE_ID);
    if (!gpio_config) {
        printf("XGpioPs_LookupConfig failed\n");
        return XST_FAILURE;
    }

    printf("gpio initialization\n");
    int status = XGpioPs_CfgInitialize(gpio, gpio_config, gpio_config->BaseAddr);
    if (status != XST_SUCCESS) {
        printf("XGpioPs_CfgInitialize failed %d\n", status);
        return status;
    }

    printf("gpio selftest\n");
    status = XGpioPs_SelfTest(gpio);
    if (status != XST_SUCCESS) {
        printf("XGpioPs_SelfTest %d\n", status);
        return status;
    }

    printf("lighting up mio7\n");
    XGpioPs_SetDirectionPin(gpio, 7, 1);
    XGpioPs_SetOutputEnablePin(gpio, 7, 1);
    XGpioPs_WritePin(gpio, 7, 1);
    return 0;
}


// Initialize the display.
static void ssd1306_reset(i2c_t *i2c, XGpioPs *gpio, int reset_pin)
{
    printf("initializing ssd1306\n");
    XGpioPs_SetDirectionPin(gpio, reset_pin, 1);
    XGpioPs_SetOutputEnablePin(gpio, reset_pin, 1);
    XGpioPs_WritePin(gpio, reset_pin, 0);
    usleep(300);
    XGpioPs_WritePin(gpio, reset_pin, 1);

    // ssd1306 initialization sequence, derived from:
    // https://github.com/Defragster/ssd1306xled/blob/master/ssd1306xled.cpp
    const u8 seq[] = {
    0xAE,           // Display OFF (sleep mode)
    0x20, 0b00,     // Set Memory Addressing Mode
                    // 00=Horizontal Addressing Mode; 01=Vertical Addressing Mode;
                    // 10=Page Addressing Mode (RESET); 11=Invalid
    0xB0,           // Set Page Start Address for Page Addressing Mode, 0-7
    0xC8,           // Set COM Output Scan Direction
    0x00,           // ---set low column address
    0x10,           // ---set high column address
    0x40,           // --set start line address
    0x81, 0x3F,     // Set contrast control register
    0xA1,           // Set Segment Re-map. A0=address mapped; A1=address 127 mapped.
    0xA6,           // Set display mode. A6=Normal; A7=Inverse
    0xA8, 0x3F,     // Set multiplex ratio(1 to 64)
    0xA4,           // Output RAM to Display
                    // 0xA4=Output follows RAM content; 0xA5,Output ignores RAM content
    0xD3, 0x00,     // Set display offset. 00 = no offset
    0xD5,           // --set display clock divide ratio/oscillator frequency
    0xF0,           // --set divide ratio
    0xD9, 0x22,     // Set pre-charge period
    0xDA, 0x12,     // Set com pins hardware configuration
    0xDB,           // --set vcomh
    0x20,           // 0x20,0.77xVcc
    0x8D, 0x14,     // Set DC-DC enable
    0xAF            // Display ON in normal mode
    };

    // Write out the oled initialization sequence.
    int i;
    for (i = 0; i < sizeof(seq); ++i) {
       i2c_command(&i2c->device, oled_addr, seq[i]);
    }
}

// Clear the display.
static void ssd1306_clear(i2c_t *i2c) {
    int i;
    ssd1306_set_page_start(i2c, 0x00);
    for (i = 0; i < (128*8); ++i) {
        i2c_data(&i2c->device, oled_addr, 0);
    }

    ssd1306_set_page_start(i2c, 0x00);
}

u32 readButtons(XGpioPs *gpio){

	u32 Buttons = 0;
	while (Buttons == 0){
		Buttons = XGpio_DiscreteRead(gpio, 1);
		if (Buttons == 1) {
			print("1\n");
			usleep(200000);
		}
		if (Buttons == 2) {
			print("2\n");
			usleep(200000);
		}
		if (Buttons == 4) {
			print("4\n");
			usleep(200000);
		}
		if (Buttons == 8) {
			print("8\n");
			usleep(200000);
		}
		if (Buttons == 16){
			print("16\n");
			usleep(200000);
		}
	}
	return Buttons;
}


u8 mainMenu(XGpioPs *gpio, i2c_t *i2c, u8 addr){
// The application entry point.

    ssd1306_clear(i2c);
    ssd1306_set_page_start(i2c, 0);
    display_string(i2c, addr, "* Pre-program");
    ssd1306_clear_line(i2c, 1);
    display_string(i2c, addr, "  User route");
    ssd1306_clear_line(i2c, 2);
    display_string(i2c, addr, "  (X,Y)");
    ssd1306_clear_line(i2c, 3);
    display_string(i2c, addr, "  Search");

    u32 selection = 0;
    u8 page = 0;
    do {
    	selection = readButtons(gpio);
    	if (selection == 8){
    		ssd1306_set_page_start(i2c, page);
    	    display_string(i2c, addr, "  ");
    		if (page == 3) {
    			page = 0;
    		} else {
    			page++;
    		}
    		ssd1306_set_page_start(i2c, page);
    	    display_string(i2c, addr, "* ");
    	}

    	if (selection == 1){
    		ssd1306_set_page_start(i2c, page);
    	    display_string(i2c, addr, "  ");
    		if (page == 0) {
    			page = 3;
    		} else {
    			page--;
    		}
    		ssd1306_set_page_start(i2c, page);
    	    display_string(i2c, addr, "* ");
    	}
    } while (selection != 16);
    ssd1306_clear(i2c);

    return page;
}


void user_input_X_Y(XGpioPs *gpio, i2c_t *i2c, u8 addr, int *xx, int*yy) {
	int x = 0;
	int y = 0;
    ssd1306_clear(i2c);
    display_string(i2c, addr, "x: 0");
    ssd1306_set_page_start(i2c, 0x01);
    display_string(i2c, addr, "y: 0");
    u32 sel = 0;
    char xChar[4];
    char yChar[4];
    do {
    	sel = readButtons(gpio);
    	if ((sel == 2)&&(x<63)){
    		x++;
    	}

    	if ((sel == 4)&&(x>-64)){
    		printf("x: %d\n", x);
    		x--;
    	}
    	if ((sel == 1)&&(y<31)){
    		y++;
    	}

    	if ((sel == 8)&&(y>-32)){
    		y--;
    	}

    	snprintf(xChar, sizeof(xChar), "%d     ", x);
		ssd1306_set_page_start(i2c, 0);
	    display_string(i2c, addr, "x: ");
	    display_string(i2c, addr, xChar);


    	snprintf(yChar, sizeof(yChar), "%d    ", y);
		ssd1306_set_page_start(i2c, 1);
	    display_string(i2c, addr, "y: ");
	    display_string(i2c, addr, yChar);
    } while (sel != 16);
    printf("x: %d\n", x);
    printf("y: %d\n", y);
	*xx = x;
    *yy = y;
    //now run the x,y coordinates
}

void user_input_FIFO(XGpioPs *gpio, i2c_t *i2c, u8 addr, char *FIFO, int size) {
    ssd1306_clear(i2c);
    display_string(i2c, addr, "Up Arrow: FWD");
    ssd1306_set_page_start(i2c, 0x01);
    display_string(i2c, addr, "Down Arrow: REV");
    ssd1306_set_page_start(i2c, 0x02);
    display_string(i2c, addr, "Left Arrow: LEFT");
    ssd1306_set_page_start(i2c, 0x03);
    display_string(i2c, addr, "Right Arrow: RIGHT");
    ssd1306_set_page_start(i2c, 0x04);
    display_string(i2c, addr, "Center: RETURN");
    u32 sel = 0;
    int i=0;
    char text[30];
    do {
    	sel = readButtons(gpio);
    	if (sel == 2){
    		FIFO[i] = 'R';
    		strcpy(text, "Right Turn    ");
    	}

    	if (sel == 4){
    		FIFO[i] = 'L';
    		strcpy(text, "Left Turn     ");
    	}
    	if (sel == 1){
    		FIFO[i] = 'F';
    		strcpy(text, "Forward       ");
    	}

    	if (sel == 8){
    		FIFO[i] = 'R';
    		strcpy(text, "Reverse       ");
    	}

    	if (sel == 16){
    		FIFO[i] = 'Q';
    		strcpy(text, "Return        "); // return to base station
    	}

    	printf("%d, %c\n", i, FIFO[i]);
    	i++;
    	char text1[30];
		ssd1306_set_page_start(i2c, 6);
    	snprintf(text1, sizeof(text1), "Task %d Scheduled", i);
	    display_string(i2c, addr, text1);

		ssd1306_set_page_start(i2c, 7);
	    display_string(i2c, addr, text);
    } while ((sel != 16)&&(i<size));
    FIFO[size-1] = 'Q';  // this makes sure the last task is a return command1

}

void user_input_search_duration(XGpioPs *gpio, i2c_t *i2c, u8 addr, int *time) {
	int t = 0;
    ssd1306_clear(i2c);
    display_string(i2c, addr, "time: 0");
    ssd1306_set_page_start(i2c, 0x01);
    display_string(i2c, addr, "5s increments");
    u32 selct = 0;
    char tChar[5];
    do {
    	selct = readButtons(gpio);
    	if (selct == 1){
    		t+=5;
    	}

    	if ((t>0)&&(selct == 8)){
    		t-=5;
    	}
    	snprintf(tChar, sizeof(tChar), "%d      ", t);

		ssd1306_set_page_start(i2c, 0);
	    display_string(i2c, addr, "time: ");
	    display_string(i2c, addr, tChar);
    } while (selct != 16);
    *time = t;
}


void write_pixel(XGpioPs *gpio, i2c_t *i2c, u8 addr, u8 x, u8 y, int map[]){
	//origin for the device is in the center of the map
	x += 64;
	y=-1*y;
	y += 32;
	u8 page = 0;
	//we no from x what column to write to, but must figure out which page to write to
	//now we figure out the page
	u8 data = 0x00;
	int i = y;
	while (i>8) {
		page++;
		i -= 8;
	}
    printf("page: %d column: %d \n", page, x);

	int indexCount = page*127*8+x;
	int placeHolder;
	for (i=0; i<8; i++) {
		placeHolder =(1<<i);
		data += placeHolder*map[indexCount];
	    printf("data: %d\n", data);
		indexCount += 128;
	}
    printf("data: %d indexCount: %d\n", data, indexCount);
	ssd1306_set_page_start(i2c, page);
	ssd1306_set_col_start(i2c, x);
	i2c_data(&i2c->device, addr, data);
	ssd1306_set_page_start(i2c, 0);
}

int main()
{
    init_platform();



    printf("Hello world\n");

    int status;

    i2c_t oled = {
        .id = XPAR_PS7_I2C_0_DEVICE_ID,
        .clock = 100000,
        .clear_options = XIICPS_10_BIT_ADDR_OPTION,
        .options = XIICPS_7_BIT_ADDR_OPTION,
    };
    status = initialize_i2c(&oled);
    if (status) {
        printf("initialize_i2c failed %d\n", status);
        return status;
    }
    int i = 0;

    XGpioPs gpio;
    status = initialize_gpio(&gpio);
    if (status) {
        printf("intialize_gpio failed %d\n", status);
        return status;
    }
    ssd1306_reset(&oled, &gpio, oled_reset_pin);
    printf("ssd1306_reset complete\n");
    ssd1306_clear(&oled);

    if (status) {
        printf("initialize_axi_gpio failed %d\n", status);
        return status;
    }

    	status = XGpio_Initialize(&gpio, XPAR_AXI_GPIO_0_DEVICE_ID);
    	if (status != XST_SUCCESS) {
    		return XST_FAILURE;
    	}

    u32 buttons = readButtons(&gpio);
    for(i=0; i< sizeof(Inspire); i++) {
    	i2c_data(&oled.device, oled_addr, Inspire[i]);
    }

    sleep(2);

 // playground area
/*
    ssd1306_clear(&oled);
    int map[8192];
    for(i = 0; i < 8192; i++) {
    	map[i] = 1;
    }
    map[0] = 1;
    map[8191] =1;

    write_pixel(&gpio, &oled, oled_addr, 0, 0, map);
    write_pixel(&gpio, &oled, oled_addr, 127, 63, map);

    write_pixel(&gpio, &oled, oled_addr, 0, 63, map);
    write_pixel(&gpio, &oled, oled_addr, 127, 0, map);
*/
// end playground area
    u8 selection = 5;

	int x, y, t;
	char task[50];
	char FIFO[50];
	printf("Size of FIFO %d\n", sizeof(FIFO));
    while (1){
    	//main menu
    	selection = mainMenu(&gpio, &oled, oled_addr);
        switch (selection) {
        	case (0) :
        		//run the preprogrammed route
        		strcpy(task, "Programmed route");
        		break;

        	case (1) :
        		//run user directed vector
				user_input_FIFO(&gpio, &oled, oled_addr, &FIFO, sizeof(FIFO));
			    printf("\n");
			    for (i=0; i<sizeof(FIFO); i++) {
			    	printf("%d, %c\n", i, FIFO[i]);
			    }
				strcpy(task, "User defined route");
        	    break;

        	case (2) :
        		//run user directed coordinate
        		user_input_X_Y(&gpio,&oled, oled_addr, &x, &y);
	    		printf("\nx %d, y %d\n", x, y);
        		strcpy(task, "X,Y Coordinate");
        		//some function in terms of x,y
        	    break;

        	case (3) :
        		//search
				user_input_search_duration(&gpio, &oled, oled_addr, &t);
    			printf("\nt %d\n",t);
				strcpy(task, "Area Map/Search");
        	    break;
        }

        ssd1306_clear(&oled);
    	display_string(&oled, oled_addr, task);
    	ssd1306_set_page_start(&oled, 1);
    	display_string(&oled, oled_addr, "Complete");
    	readButtons(&gpio);
    }
    return 0;
}
