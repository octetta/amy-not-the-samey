// amy-message.c
// hacked from amy-example.c, this code shows using miniaudio and allows entry it AMY ASCII commands.

#include <math.h>
#include <sys/param.h>
#include <unistd.h>

#include "amy.h"
#include "pcm.h"

#include "bestline.h"
#include "libminiaudio-audio.h"

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

////

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int utf8_encode(char *out, uint32_t utf);

/*

>>> bytes(chr(0x2800), 'utf8')
b'\xe2\xa0\x80'

#include <string.h>

void *memset(void *s, int c, size_t n);

*/

#define ROWS (36)
#define COLS (162)
#define _ROWS (ROWS/4)
#define _ZERO (_ROWS/2)
#define _COLS (COLS/2)

uint16_t  canvas[(ROWS>>2)*(COLS>>1)];
uint16_t  colors[(ROWS>>2)*(COLS>>1)];

char upper_left[32];
char left[32];
char lower_left[32];
char upper_right[32];
char right[32];
char lower_right[32];
char tleft[32];
char tright[32];
char mdot[32];
char rarrow[32];
char middle[32];
char *reset = "\033[39;49m";
char *clear = "\033[2J";
char *home = "\033[H";

void cls(void) {
    memset(canvas, 0, sizeof(canvas));
    memset(colors, 0, sizeof(colors));
}

uint16_t pixel_map[4][2] = {
    {0x01, 0x08},
    {0x02, 0x10},
    {0x04, 0x20},
    {0x40, 0x80},
};

double map(
    double x,
    double in_min,
    double in_max,
    double out_min,
    double out_max) {
    double n = (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
    return n;
}

void set(int16_t x, int16_t y, int16_t c, char force) {
    if (x < 0) return;
    if (y < 0) return;
    if (x > COLS-1) return;
    if (y > ROWS-1) return;
    uint16_t p = pixel_map[y % 4][x % 2];
    x /= 2;
    y /= 4;
    // if (x > _COLS) x = _COLS-1; 
    // if (y > _ROWS) y = _ROWS-1; 
    canvas[y * _COLS + x] |= p;
    if (force) {
        colors[y * _COLS + x] = c;
        return;
    }
    int16_t pc = colors[y * _COLS + x];
    if (pc == 0) {
        colors[y * _COLS + x] = c;
    }
}

#define UNICODE_BOX (0x2500)

#define UNICODE_BRAILLE (0x2800)
//               0123456789 len:10
//                      CC
char _fg[] = "\033[38;5;32m";
char *_fg_color[] = {
    "\033[30m", // 0 black
    "\033[31m", // 1 red
    "\033[32m", // 2 green
    "\033[33m", // 3 yellow
    "\033[34m", // 4 blue
};

#define BLACK  0
#define RED    1
#define GREEN  2
#define YELLOW 3
#define BLUE   4

void show(char more) {
    uint32_t x, y;
    char raw[32];
    for (y = 0; y < _ROWS; y++) {
        write(1, reset, strlen(reset));
        if (y == 0) {
            write(1, " +", 2);
            write(1, upper_left, strlen(upper_left));
        } else if (y == _ZERO) {
            write(1, " 0", 2);
            write(1, tright, strlen(tright));
        } else if (y == (_ROWS-1)) {
            write(1, " -", 2);
            write(1, lower_left, strlen(lower_left));
        } else {
            write(1, "  ", 2);
            write(1, left, strlen(left));
        }
        for (x = 0; x < _COLS; x++) {
            uint32_t offset = y * _COLS + x;
            uint16_t b = canvas[offset];
            if (b) {
                int16_t c = colors[offset];
                if (c >= 0) {
                    char *s = _fg_color[c];
                    write(1, s, strlen(s));
                }
                b += UNICODE_BRAILLE;
                int n = utf8_encode(raw, b);
                write(1, raw, n);
            } else {
                if ((y%2)==0) {
                    if ((x%4)==0) {
                        //             123
                        //write(1, "\033[m", 3);
                        //           1234567890123456
                        write(1, "\033[38;2;85;85;85m", 16);
                        write(1, mdot, strlen(mdot));
                    } else {
                        write(1, " ", 1);
                    }
                } else {
                    write(1, " ", 1);
                }
            }
        }
        write(1, reset, strlen(reset));
        if (y == 0) {
            write(1, upper_right, strlen(upper_right));
        } else if (y == (_ROWS-1)) {
            write(1, lower_right, strlen(lower_right));
        } else if (y == _ZERO) {
            write(1, tleft, strlen(tleft));
        } else if (y == 1) {
            if (more) {
                write(1, tright, strlen(tright));
                write(1, middle, strlen(middle));
                write(1, rarrow, strlen(rarrow));
            } else {
                write(1, right, strlen(right));
            }
        } else {
            write(1, right, strlen(right));
        }
        write(1, "\n", 1);
    }
}

/**
 * Encode a code point using UTF-8
 * 
 * @author Ondřej Hruška <ondra@ondrovo.com>
 * @license MIT
 * 
 * @param out - output buffer (min 5 characters), will be 0-terminated
 * @param utf - code point 0-0x10FFFF
 * @return number of bytes on success, 0 on failure (also produces U+FFFD, which uses 3 bytes)
 */
int utf8_encode(char *out, uint32_t utf) {
    if (utf <= 0x7F) {
        // Plain ASCII
        out[0] = (char) utf;
        out[1] = 0;
        return 1;
    } else if (utf <= 0x07FF) {
        // 2-byte unicode
        out[0] = (char) (((utf >> 6) & 0x1F) | 0xC0);
        out[1] = (char) (((utf >> 0) & 0x3F) | 0x80);
        out[2] = 0;
        return 2;
    } else if (utf <= 0xFFFF) {
        // 3-byte unicode
        out[0] = (char) (((utf >> 12) & 0x0F) | 0xE0);
        out[1] = (char) (((utf >>  6) & 0x3F) | 0x80);
        out[2] = (char) (((utf >>  0) & 0x3F) | 0x80);
        out[3] = 0;
        return 3;
    } else if (utf <= 0x10FFFF) {
        // 4-byte unicode
        out[0] = (char) (((utf >> 18) & 0x07) | 0xF0);
        out[1] = (char) (((utf >> 12) & 0x3F) | 0x80);
        out[2] = (char) (((utf >>  6) & 0x3F) | 0x80);
        out[3] = (char) (((utf >>  0) & 0x3F) | 0x80);
        out[4] = 0;
        return 4;
    } else { 
        // error - use replacement character
        out[0] = (char) 0xEF;  
        out[1] = (char) 0xBF;
        out[2] = (char) 0xBD;
        out[3] = 0;
        return 0;
    }
}
////

int main(int argc, char ** argv) {
    int use_history = -1;
    int show_sysclock = 0;
    int verbose = 0;
    int opt;
    double ms_per_block = 1000.0 / SAMPLE_RATE * BLOCK_SIZE;
    utf8_encode(left, 0x2503);
    utf8_encode(upper_left, 0x250f);
    utf8_encode(lower_left, 0x2517);
    utf8_encode(right, 0x2503);
    utf8_encode(upper_right, 0x2513);
    utf8_encode(lower_right, 0x251b);
    utf8_encode(tleft, 0x252b);
    utf8_encode(tright, 0x2523);
    utf8_encode(middle, 0x2501);
    utf8_encode(rarrow, 0x2bc8);
    utf8_encode(mdot, 0x22c5);
    while((opt = getopt(argc, argv, ":d:c:lhnsV")) != -1) 
    { 
        switch(opt) 
        { 
            case 'V':
                verbose = 1;
                break;
            case 's':
                show_sysclock = 1;
                break;
            case 'n':
                use_history = 0;
                break;
            case 'd':
                amy_device_id = atoi(optarg);
                break;
            case 'c':
                amy_channel = atoi(optarg);
                break; 
            case 'l':
                amy_print_devices();
                return 0;
                break;
            case 'h':
                printf("usage: amy-example\n");
                printf("\t[-d sound device id, use -l to list, default, autodetect]\n");
                printf("\t[-c sound channel, default -1 for all channels on device]\n");
                printf("\t[-l list all sound devices and exit]\n");
                printf("\t[-n don't use command line editing or history]\n");
                printf("\t[-s show sysclock on each prompt]\n");
                printf("\t[-V verbose output]\n");
                printf("\t[-h show this help and exit]\n");
                return 0;
                break;
            case ':': 
                printf("option needs a value\n"); 
                break; 
            case '?': 
                printf("unknown option: %c\n", optopt);
                break; 
        } 
    }

    if (use_history < 0) {
        if (isatty(0)) use_history = 1;
        else use_history = 0;
    }

    amy_start();
    amy_live_start();
    amy_reset_oscs();

    if (verbose) {
        printf("# # amy-message AMY playground -> https://octetta.com\n");
        printf("# - uses AMY audio synthesizer library -> https://github.com/bwhitman/amy\n");
        printf("# - uses miniaudio v0.11.11 audio playback library -> https://miniaud.io\n");
        printf("# - uses bestline history and editing library -> https://github.com/jart/bestline\n");
        printf("# OSCS=%d\n", OSCS);
        printf("# SAMPLE_RATE=%d\n", amy_sample_rate(0));
        printf("# BLOCK_SIZE=%d\n", BLOCK_SIZE);
        printf("# BLOCK_COUNT=%d\n", BLOCK_COUNT);
        double msec = (double)(1000.0/SAMPLE_RATE*BLOCK_SIZE);
        printf("# MS_PER_BLOCK=%.03f\n", msec);
        msec *= BLOCK_COUNT;
        printf("# MAX_CAPTURE_MS=%.03f\n", msec);
    }

    char *history_file = "amy-message-history.txt";

    if (use_history) {
        printf("# loaded history from %s\n", history_file);
        bestlineHistoryLoad(history_file);
    } else {
        printf("# history disabled\n");
    }

#define MAX_MESSAGES (32)
#define LEN_MESSAGES (1024)

    char messages[MAX_MESSAGES][LEN_MESSAGES];
    char timestamp[48];
    char prompt[48] = ": ";
    char *input = NULL;
    int now = amy_sysclock();

#if 0
    // hoping this makes timestamp weirdness go away...
    char amy_init_string[64];
    sprintf(amy_init_string, "t%dv0w0f10l.1", now + 10);
    amy_play_message(amy_init_string);
    sprintf(amy_init_string, "t%dv0l0", now + 50);
    amy_play_message(amy_init_string);
#endif

    while (1) {
        int process_wire = -1;
        
        if (show_sysclock) {
            sprintf(prompt, "@%d : ", now);
        }
        
        if (use_history) {
            input = bestline(prompt);
        } else {
            input = (char *)malloc(LEN_MESSAGES);
        }
        
        if (input == NULL) break;
        
        if (use_history) {
            bestlineHistoryAdd(input);
            bestlineHistorySave("amy-message-history.txt");
        } else {
            write(1, prompt, strlen(prompt));
            int n = read(0, input, LEN_MESSAGES-1);
            if (n == 0) {
                write(1, "\n", 1);
                break;
            }
            if (n > 0) {
                input[n] = '\0';
            } else {
                break;
            }
        }
        
        int t0, t1;

        if (input[0] == '?') {
            if (input[1] == '\0') {
                double msec = (double)(1000.0/SAMPLE_RATE*(BLOCK_COUNT*BLOCK_SIZE));
                puts("# amy-message specials");
                puts("@ -> show amy-message stats");
                puts("= -> show captured data to stdout as JSON");
                puts("% -> show captured data as waveform");
                puts(") --> save captured data on PCM sample slot 67");
                puts("! -> write captured data to a JSON file");
                puts("^ -> write captured data to a WAV file");
                puts("~ -> show potential zero left and right trimming index");
                printf(">[0-9]+ -> capture frames for specified msec (max = %d)\n", (int)msec);
                puts("< -> stop capture");
                puts("-> comment / ignore until end-of-line");
                puts("# otherwise, AMY wire protocol plus...");
                puts("precede with +[0-9]+ to create timestamp of NOW + specified msec");
                puts("separate multiple oscillator (v) messages with ;");
                puts("# more info via");
                puts("?? -> show AMY commands");
                puts("?e -> show AMY examples");
                puts("?f -> show FM patch list");
                puts("?p -> show 'partials' patch list");
                puts("?s -> show PCM sample list");
            } else if (input[1] == '?') {
                puts("# AMY wire protocol");
                puts("a amp without retrigger 0.0->1.0+       | A bp0");
                puts("b algo feedback 0.0->1.0                | B bp1");
                puts("d duty cycle of pulse 0.01->0.99        | C bp2");
                puts("f frequency of osc                      | D debug");
                puts("g modulation tarGet                     | F center freq for filter");
                puts("  1=amp         2=duty  4=freq          | G filter 0=none 1=LPF 2=BPF 3=HPF");
                puts("  8=filter-freq 16=rez  32=feedback     | I ratio (see AMY webpage)");
                puts("l velocity/trigger >0.0 or note-off 0   | L mod source osc / LFO");
                puts("n midi note number                      | N latency ms");
                puts("o DX7 algorithm 1-32                    | O oscs algo 6 comma-sep, -1=none");
                puts("p patch                                 | P phase 0.0->1.0 where osc cycle starts");
                puts("t timestamp in ms for event             | R rez q filter 0.0->10.0, default 0.7");
                puts("v osc selector 0->63                    | S reset oscs");
                puts("w wave type                             | T bp0 target");
                puts("  0=sine 1=pulse  2=saw-dn 3=saw-up     | W bp1 target");
                puts("  4=tri  5=noise  6=ks     7=pcm        | X bp2 target");
                puts("  8=algo 9=part  10=parts 11=off        | V volume knob for everything");
                puts("x eq_l in dB fc=800Hz -15.0->15.0 0=off");
                puts("y eq_m in dB fc=2500Hz     \"");
                puts("z eq_h in dB fc=7500Hz     \"");
            } else if (input[1] == 'e') {
                // show AMY examples
                puts("# Joe's examples");
                puts("# 001 / FM bell patch midi key 69 = A440");
                puts("S65;+0v3w8p230n69l1;+1000v3l0");
                puts("# 002 / FM bell patch midi key 70");
                puts("S65;+0v3w8p230n70l1;+1000v3l0");
                puts("# 003 / FM scifi patch");
                puts("S65;+0v3w8p1023n20l1;+1000v3l0");
                puts("# 004 / FM pad");
                puts("S65;+0v3w8p1n69l1;+1000v3l0");
                puts("# 005/ 220 Hz sine wave");
                puts("S65;v0w0f220l1");
                puts("# 006/ 221 Hz sine wave");
                puts("v1w0f220l1");
                puts("# 007 / turn off osc 0 & 1");
                puts("v0l0;v1l0");
                puts("# 008/ sine w/filter ASDR");
                puts("S65;v0f220w1F2500R5A100,0.5,5000,0T8G1;+1000v0l1;+1500v0l0");
                puts("# 009 / noise with filter ADSR");
                puts("S65;v1f220w5F2500R5A100,0.5,5000,0T8G1;+0v1l5;+500v1l0");
                puts("# 010 / wave with filter ASDR");
                puts("S65;v0f220w2F2500R5A100,0.5,2500,0T8G1;+0v0l1;+1000v0l0");
                puts("# Adapted amy.py examples");
                puts("# simple note");
                puts("v0w0A10,1,250,0.7,500,0T1");
                puts("v0w0A10,1,250,0.7,500,0T1;v0f440;+500v0l1;+1000v0l0");
                puts("# filter bass");
                puts("v0F2500R5w2G0A100,0.5,25,0T9;v0f110;+500v0l2;+1000v0l0");
                puts("# long sine pad to test ADSR");
                puts("v0w0A0,0,500,1,1000,0.25,750,0T1;v0f440;+500v0l2;+1000v0l0");
                puts("# amp LFO example");
                puts("v1w0l.5f1.5");
                puts("v0w1A150.1.250.0.25,250.0T1g1L1");
                puts("v0f440");
                puts("+10v0l1;+2000v0l0");
                puts("# pitch LFO going up");
                puts("v1w0l.5f.25P.5");
                puts("v0w1A150,1,400,0,0,0T1g5L1");
                puts("v0f440l1");
                puts("# bass drum : uses a 0.25Hz sine at 0.5 phase (going down) to modify freq of another sine");
                puts("v1w0l.5f.25P.5");
                puts("v0w0l0A500,0,0,0T1g4L1");
                puts("v0f110l5");
                puts("# noise snare");
                puts("v0w5A250,0,0,0T1");
                puts("v0l1");
                puts("# closed hat");
                puts("v0w5l0A25,1,75,0,0,0T1");
                puts("v0l5");
                puts("# closed hat from PCM");
                puts("v0w7l0p0f0");
                puts("v0l5");
                puts("# cowbell from PCM");
                puts("v0w7l0p10f0");
                puts("v0l5");
                puts("# high cowbell from PCM");
                puts("v0w7l0p10f0");
                puts("v0n70l5");
                puts("# snare from PCM");
                puts("v0w7l0p5f0");
                puts("v0l1");
                puts("# FM bass");
                puts("v0w8l0p21");
                puts("v0f220l5");
                puts("# Pcm bass drum");
                puts("v0w7l0p1f0");
                puts("v0l10");
                puts("# filtered algo");
                puts("v0w8l0p62F2000R2.5G1T8A1,1,500,0,0,0");
                puts("v0n69l10");
            } else if (input[1] == 'f') {
                // show FM patches list
                puts("# FM (algo) patches -> w8");
                /*
 0 BRASS 1     16 E.ORGAN 1   32 PIANO 4     48 PIPES 2
 1 BRASS 2     17 PIPES 1     33 PIANO 5     49 PIPES 3
 2 BRASS 3     18 HARPSICH 1  34 E.PIANO 2   50 PIPES 4
 3 STRINGS 1   19 CLAV 1      35 E.PIANO 3   51 CALIOPE
 4 STRINGS 2   20 VIBE 1      36 E.PIANO 4   52 ACCORDION
 5 STRINGS 3   21 MARIMBA     37 PIANO 5THS  53 SITAR
 6 ORCHESTRA   22 KOTO        38 CELESTE     54 GUITAR 3
 7 PIANO 1     23 FLUTE 1     39 TOY PIANO   55 GUITAR 4
 8 PIANO 2     24 ORCH-CHIME  40 HARPSICH 2  56 GUITAR 5
 9 PIANO 3     25 TUB BELLS   41 HARPSICH 3  57 GUITAR 6
10 E.PIANO 1   26 STEEL DRUM  42 CLAV 2      58 LUTE
11 GUITAR 1    27 TIMPANI     43 CLAV 3      59 BANJO
12 GUITAR 2    28 REFS WHISL  44 E.ORGAN 2   60 HARP 1
13 SYN-LEAD 1  29 VOICE 1     45 E.ORGAN 3   61 HARP 2
14 BASS 1      30 TRAIN       46 E.ORGAN 4   62 BASS 3
15 BASS 2      31 TAKE OFF    47 E.ORGAN 5   63 BASS 4

64 PICCOLO     80 RECORDER    96 SYN-LEAD 2   112 HARP-FLUTE
65 FLUTE 2     81 HARMONICA1  97 SYN-LEAD 3   113 BELL-FLUTE
66 OBOE        82 HRMNCA2 BC  98 SYN-LEAD 4   114 E.P-BRS BC
67 CLARINET    83 VOICE 2     99 SYN-LEAD 5   115 T.BL-EXPA
68 SAX BC      84 VOICE 3     100 SYN-CLAV 1  116 CHIME-STRG
69 BASSOON     85 GLOKENSPL   101 SYN-CLAV 2  117 B.DRM-SNAR
70 STRINGS 4   86 VIBE 2      102 SYN-CLAV 3  118 SHIMMER
71 STRINGS 5   87 XYLOPHONE   103 SYN-PIANO   119 EVOLUTION
72 STRINGS 6   88 CHIMES      104 SYNBRASS 1  120 WATER GDN
73 STRINGS 7   89 GONG 1      105 SYNBRASS 2  121 WASP STING
74 STRINGS 8   90 GONG 2      106 SYNORGAN 1  122 LASER GUN
75 BRASS 4     91 BELLS       107 SYNORGAN 2  123 DESCENT
76 BRASS 5     92 COW BELL    108 SYN-VOX     124 OCTAVE WAR
77 BRASS 6 BC  93 BLOCK       109 SYN-ORCH    125 GRAND PRIX
78 BRASS 7     94 FLEXATONE   110 SYN-BASS 1  126 ST.HELENS
79 BRASS 8     95 LOG DRUM    111 SYN-BASS 2  127 EXPLOSION

128 - FLUTE 1 
129 - HARPSICH 1 
130 - STRG ENS 1 
131 - BRIGHT BOW 
132 - BRASSHORNS 
133 - BR TRUMPET 
134 - MARIMBA 
135 - E.PIANO 1 
136 - PIANO 1 
137 - PIPES 1 
138 - E.ORGAN 1 
139 - E.BASS 1 
140 - CLAV 1 
141 - HARMONICA1 
142 - JAZZ GUIT1 
143 - PRC SYNTH1 
144 - SAX BC 
145 - FRETLESS 1 
146 - HARP 1 
147 - TIMPANI 
148 - HEAVYMETAL 
149 - STEEL DRUM 
150 - SYN-LEAD 1 
151 - VOICES BC 
152 - CLAV ENS 
153 - LASERSWEEP 
154 - TUB ERUPT 
155 - GRAND PRIX 
156 - REFS WHISL 
157 - TRAIN 
158 - BRASS S H 
159 - TAKE OFF 
160 - PIANO 2 
161 - E.GRAND 1 
162 - E.GRAND 2 
163 - HONKY TONK 
164 - E.PIANO 2 
165 - E.PIANO 3 
166 - E.PIANO 4 
167 - CELESTE 
168 - FUNK CLAV 
169 - CLAV ENS 2 
170 - PERC CLAV 
171 - HARPSICH 2 
172 - E.ORGAN 2 
173 - E.ORGAN 3 
174 - 60-S ORGAN 
175 - PIPES 2 
176 - PIPES 3 
177 - CALIOPE 
178 - ACCORDION 
179 - TOY PIANO 
180 - SITAR 
181 - KOTO 
182 - JAZZ GUIT2 
183 - SPANISHGTR 
184 - FOLK GUIT 
185 - LUTE 
186 - BANJO 
187 - CLAS.GUIT 
188 - HARP 2 
189 - E.BASS 2 
190 - FRETLESS 2 
191 - PLUCK BASS 
192 - PICCOLO 
193 - FLUTE 2 
194 - OBOE 
195 - CLARINET 
196 - BASSOON 
197 - PAN FLUTE 
198 - LEAD BRASS 
199 - HORNS 
200 - SOLO TBONE 
201 - BRASS BC 
202 - BRASS 5THS 
203 - SYNTHBRASS 
204 - STRG QRT 1 
205 - STRG ENS 2 
206 - VIOLA SECN 
207 - STRGS LOW 
208 - HIGH STRGS 
209 - PIZZ STGS 
210 - STG CRSNDO 
211 - STGS 5THS 
212 - BELLS 
213 - TUB BELLS 
214 - RECORDERS 
215 - CHIMES 
216 - VOICES 
217 - XYLOPHONE 
218 - COWBELL 
219 - WOOD BLOCK 
220 - FLEXATONE 
221 - LOG DRUM 
222 - GLOKENSPL 
223 - VIBE 
224 - CLAV-E.PNO 
225 - PERC BRASS 
226 - PRC SYNTH2 
227 - HARPSI-STG 
228 - CHIME-STRG 
229 - HARP-FLUTE 
230 - BELL-FLUTE 
231 - STRG-CHIME 
232 - STRG-MARIM 
233 - STRG-PIZZT 
234 - ORCHESTRA 
235 - LEAD GUITR 
236 - PIANO-BRS 
237 - BRS-CHIME 
238 - B.DRM-SNAR 
239 - E.P-BRS BC 
240 - ORG-BRS BC 
241 - CLV-BRS BC 
242 - WHISTLES 
243 - FILTER SWP 
244 - FUNKY RISE 
245 - WILD BOAR 
246 - SHIMMER 
247 - EVOLUTION 
248 - WATER GDN 
249 - WASP STING 
250 - MULTI NOTE 
251 - DESCENT 
252 - OCTAVE WAR 
253 - ..GOTCHA.. 
254 - ST.HELENS 
255 - EXPLOSION 
256 - PIANO 1 
257 - PIANO 2 
258 - PIANO 3 
259 - HONKEY.P.F 
260 - PLEPIARD 
261 - PIANO-CP 
262 - E.PIANO 1 
263 - E.PIANO 2 
264 - E.PIANO 3 
265 - E.PIANO 4 
266 - CEMBALO 1 
267 - CEMBALO 2 
268 - E-CEMBALO 
269 - CLAVI. 1 
270 - CLAVI. 2 
271 - CLAVI. 3 
272 - CLAVI. 4 
273 - CELESTE 1 
274 - CELESTE 2 
275 - AC.GUITAR1 
276 - AC.GUITAR2 
277 - AC.GUITAR3 
278 - AC.GUITAR4 
279 - 12S.GUITAR 
280 - E.GUITAR 1 
281 - E.GUITAR 2 
282 - E.GUITAR 3 
283 - E.GUITAR 4 
284 - E.GUITAR 5 
285 - E.BASS 1 
286 - E.BASS 2 
287 - E.BASS 3 
288 - E.BASS 4 
289 - E.BASS 5 
290 - SYN.BASS 1 
291 - SYN.BASS 2 
292 - SYN.BASS 3 
293 - SYN.BASS 4 
294 - SYN.BASS 5 
295 - SYN.BASS 6 
296 - W.BASS 1 
297 - W.BASS 2 
298 - W.BASS 3 
299 - W.BASS 4 
300 - ZITAR 
301 - ZIMBALONE 
302 - BAMBOO MAR 
303 - KARIMBA 
304 - MANDOLIN 1 
305 - MANDOLIN 2 
306 - SITAR 
307 - BANJO 1 
308 - BANJO 2 
309 - CHALANGO 
310 - SHIYAMISEN 
311 - BIWA 
312 - VIB. 1 
313 - VIB. 2 
314 - GLOCKEN 1 
315 - GLOCKEN 2 
316 - XYLOPHONE 
317 - MARIMBA 
318 - HANDBELL 1 
319 - HANDBELL 2 
320 - PICCOLO 1 
321 - PICCOLO 2 
322 - FLUTE 1 
323 - FLUTE 2 
324 - FLUTE 3 
325 - ALTO FLUTE 
326 - BASS FLUTE 
327 - OBOE 1 
328 - OBOE 2 
329 - ENG.HORN 
330 - CLARINET 1 
331 - CLARINET 2 
332 - CLARINET 3 
333 - BASS CLA. 
334 - BASSOON 1 
335 - BASSOON 2 
336 - BASSOON 3 
337 - SAXOPHONE1 
338 - SAXOPHONE2 
339 - SAXOPHONE3 
340 - SAXOPHONE4 
341 - HORN 1 
342 - HORN 2 
343 - HORN 3 
344 - HORN 4 
345 - HORN 5 
346 - TRUMPET 1 
347 - TRUMPET 2 
348 - TRUMPET 3 
349 - TRUMPET 4 
350 - TRUMPET 5 
351 - TRUMPET 6 
352 - TRUMPET 7 
353 - P.TRUMPET 
354 - TPT.MUTE 1 
355 - TPT.MUTE 2 
356 - FLUGELHORN 
357 - TROMBONE 1 
358 - TROMBONE 2 
359 - TROMBONE 3 
360 - TRB.MUTE 1 
361 - TRB.MUTE 2 
362 - TUBA 
363 - RECORDER 
364 - T.RECORDER 
365 - OCARINA 
366 - QUENA 
367 - BAMBOO FL. 
368 - PANFLUTE1 
369 - PANFLUTE2 
370 - BAGPIPE 
371 - HARMONICA1 
372 - HARMONICA2 
373 - ACCORDION1 
374 - ACCORDION2 
375 - BRASS EN.1 
376 - BRASS EN.2 
377 - BRASS EN.3 
378 - BRASS EN.4 
379 - BRASS EN.5 
380 - BRASS EN.6 
381 - BRASS EN.7 
382 - WIND ENS.1 
383 - WIND ENS.2 
384 - VIOLIN 1 
385 - VIOLIN 2 
386 - VIOLIN 3 
387 - VIOLIN 4 
388 - CELLO 1 
389 - CELLO 2 
390 - CONTRABASS 
391 - STRINGS 1 
392 - STRINGS 2 
393 - STRINGS 3 
394 - STRINGS 4 
395 - STRINGS 5 
396 - STRINGS 6 
397 - STRINGS 7 
398 - STRINGS 8 
399 - STRINGS 9 
400 - STRINGS 10 
401 - STRINGS 11 
402 - STR.PIZZ.1 
403 - STR.PIZZ.2 
404 - STR.PIZZ.3 
405 - STR.PIZZ.4 
406 - M. VOICE 1 
407 - M. VOICE 2 
408 - M. VOICE 3 
409 - M. VOICE 4 
410 - M. VOICE 5 
411 - M. VOICE 6 
412 - F. VOICE 1 
413 - F. VOICE 2 
414 - F. VOICE 3 
415 - F. VOICE 4 
416 - M.CHORUS 1 
417 - M.CHORUS 2 
418 - M.CHORUS 3 
419 - M.CHORUS 4 
420 - F.CHORUS 1 
421 - F.CHORUS 2 
422 - F.CHORUS 3 
423 - CHORUS 1 
424 - CHORUS 2 
425 - CHORUS 3 
426 - CHORUS 4 
427 - CHORUS 5 
428 - CHORUS 6 
429 - VOC.VOICE 
430 - WHISTLE 1 
431 - WHISTLE 2 
432 - E. ORGAN 1 
433 - E. ORGAN 2 
434 - E. ORGAN 3 
435 - E. ORGAN 4 
436 - E. ORGAN 5 
437 - E. ORGAN 6 
438 - E. ORGAN 7 
439 - E. ORGAN 8 
440 - E. ORGAN 9 
441 - E. ORGAN10 
442 - BARREL ORG 
443 - PIPE ORG.1 
444 - PIPE ORG.2 
445 - PIPE ORG.3 
446 - PIPE ORG.4 
447 - PIPE ORG.5 
448 - DRUMS 
449 - SNARE DR.1 
450 - SNARE DR.2 
451 - TOM-TOM 1 
452 - TOM-TOM 2 
453 - BASS DR. 1 
454 - BASS DR. 2 
455 - SIZ. CYMB. 
456 - HI-HAT 
457 - E. DRUMS 1 
458 - E. DRUMS 2 
459 - E. DRUMS 3 
460 - E. DRUMS 4 
461 - E. DRUMS 5 
462 - TIMBALES 1 
463 - TIMBALES 2 
464 - CONGAS 1 
465 - CONGAS 2 
466 - JPN.DRUM 1 
467 - JPN.DRUM 2 
468 - JPN.DRUM 3 
469 - JPN.DRUM 4 
470 - QUIEKER 
471 - TIMPANI 1 
472 - TIMPANI 2 
473 - HAND CLAP1 
474 - HAND CLAP2 
475 - TAP SLAP 
476 - W. BLOCK 1 
477 - W. BLOCK 2 
478 - CASTANET 1 
479 - CASTANET 2 
480 - CLAVES 
481 - GUIRO 
482 - MARACAS 
483 - KABASA 
484 - TAMBLN. 1 
485 - TAMBLN. 2 
486 - SAMBA WHIS 
487 - TRIANGLE 1 
488 - TRIANGLE 2 
489 - BELL TREE 
490 - SMALL BELL 
491 - AGOGO 
492 - GONG 
493 - COW BELL 1 
494 - COW BELL 2 
495 - COW BELL 3 
496 - WIND BELL1 
497 - WIND BELL2 
498 - STEEL DRUM 
499 - GLASSHARP1 
500 - GLASSHARP2 
501 - REV.CYMB 1 
502 - REV.CYMB 2 
503 - SYN.PERC.1 
504 - SYN.PERC.2 
505 - SYN.PERC.3 
506 - SYN.PERC.4 
507 - SYN.PERC.5 
508 - SYN.PERC.6 
509 - SYN.PERC.7 
510 - SYN.PERC.8 
511 - SYN.PERC.9 
512 - HEAVY RAIN 
513 - THUNDER 
514 - STORM 1 
515 - STORM 2 
516 - WATERDROP 
517 - BUBBLES 
518 - WAVE 1 
519 - WAVE 2 
520 - WAVE 3 
521 - WAVE 4 
522 - RUMB.EARTH 
523 - EXPLOSION1 
524 - EXPLOSION2 
525 - MAC.GUN 1 
526 - MAC.GUN 2 
527 - MOBILE 1 
528 - MOBILE 2 
529 - KLAXON 
530 - MOTERCYCLE 
531 - AMBULANCE 
532 - PATROL CAR 
533 - SIREN 
534 - PLAIN 1 
535 - PLAIN 2 
536 - HELICOPTER 
537 - LOCOMOTIVE 
538 - SHIP...... 
539 - TEL CALL 1 
540 - TEL CALL 2 
541 - BELL...... 
542 - PI.PO.PA. 
543 - ALARM! 
544 - DOG 
545 - BABY CAT 
546 - INSECTS 1 
547 - INSECTS 2 
548 - INSECTS 3 
549 - BIRD 1 
550 - BIRD 2 
551 - BIRD 3 
552 - BIRD 4 
553 - HEART BEAT 
554 - TYPEWRITER 
555 - CLOSING 
556 - FACTORY 
557 - ROBOT 
558 - TEMP.BELL1 
559 - TEMP.BELL2 
560 - CHURCHBELL 
561 - BIG BEN 
562 - IMAGE 1 
563 - IMAGE 2 
564 - IMAGE 3 
565 - IMAGE 4 
566 - IMAGE 5 
567 - IMAGE 6 
568 - IMAGE 7 
569 - IMAGE 8 
570 - IMAGE 9 
571 - IMAGE 10 
572 - IMAGE 11 
573 - IMAGE 12 
574 - IMAGE 13 
575 - IMAGE 14 
576 - SYN.STR. 1 
577 - SYN.STR. 2 
578 - SYN.STR. 3 
579 - SYN.STR. 4 
580 - SYN.STR. 5 
581 - SYN.STR. 6 
582 - SYN.STR. 7 
583 - SYN.STR. 8 
584 - SYN.STR. 9 
585 - SYN.STR.10 
586 - SYN.STR.11 
587 - SYN.STR.12 
588 - SYN.STR.13 
589 - SYN.BRA. 1 
590 - SYN.BRA. 2 
591 - SYN.BRA. 3 
592 - SYN.BRA. 4 
593 - SYN.BRA. 5 
594 - SYN.BRA. 6 
595 - SYN.BRA. 7 
596 - SYN.BRA. 8 
597 - SYN.BRA. 9 
598 - SYN.BRA.10 
599 - SYN.BRA.11 
600 - SYN.BRA.12 
601 - SYN.BRA.13 
602 - SYN.LEAD 1 
603 - SYN.LEAD 2 
604 - SYN.LEAD 3 
605 - SYN.LEAD 4 
606 - SYN.LEAD 5 
607 - SYN.LEAD 6 
608 - SYN.BASS 7 
609 - SYN.BASS 8 
610 - SYN.BASS 9 
611 - SYN.BASS10 
612 - SYN. VOX 1 
613 - SYN. VOX 2 
614 - SYN. VOX 3 
615 - SYN. VOX 4 
616 - SYN. VOX 5 
617 - SYN. VOX 6 
618 - SYN.ORG. 1 
619 - SYN.ORG. 2 
620 - SYN.ORG. 3 
621 - SYN.ORG. 4 
622 - SYN.ORG. 5 
623 - SYN.ORG. 6 
624 - SYN.MARIM. 
625 - SYN.CLAVI. 
626 - SYN.HARMO. 
627 - SYN.ORCHE. 
628 - SYN.CHIM.1 
629 - SYN.CHIM.2 
630 - SYN.S.DRUM 
631 - SYN.RECOR. 
632 - SYN.GUITAR 
633 - SYN.FLUTE 
634 - SYN.XYLOPH 
635 - SYN.SITAR 
636 - SYN.GLOC.1 
637 - SYN.GLOC.2 
638 - SYN.GLOC.3 
639 - SYN.GLOC.4 
640 - Chorus PNO 
641 - Tight PNO 
642 - Rock PNO 
643 - School PNO 
644 - Wire PNO 
645 - <Pop> Epno 
646 - <Brr> Epno 
647 - <Wap> Epno 
648 - <Puf> Orgn 
649 - <Tap> Orgn 
650 - <Big> Orgn 
651 - Magicorgan 
652 - Bigpipes 
653 - <FM>alog 1 
654 - <FM>alog 2 
655 - <FM>alog 3 
656 - <Pluk> 1 
657 - <Pluk> 2 
658 - <Pluk> 3 
659 - Hav-a-Clav 
660 - Magiklav 
661 - Tenor vox 
662 - Lady vox 
663 - Vocoda vox 
664 - Wahhh vox 
665 - Elec Brass 
666 - Elec Viola 
667 - Violin 
668 - Hall Orch 
669 - 1 fifth up 
670 - Magiglokkk 
671 - Magivybe 
672 - Fingapikka 
673 - Leadapikka 
674 - Steelypika 
675 - Classipika 
676 - Harpsipika 
677 - Pan floot 
678 - Huffsynth 
679 - Trompe 
680 - Englishorn 
681 - Harmosynth 
682 - <FM>alog 4 
683 - Backtalk 
684 - Newtrumpet 
685 - Elec comb 
686 - Touchbend 
687 - Bell<Wahh> 
688 - Bowed bell 
689 - Ting voice 
690 - Cymbal 
691 - Rubbergong 
692 - Long gong 
693 - Smooh bass 
694 - Bowed bass 
695 - Oww bass 
696 - Bass bass 
697 - Flunk bass 
698 - Chain saw 
699 - Tambourine 
700 - Thunderon 
701 - <<Smash>> 
702 - Handrum 
703 - Congadrum 
704 - PickGuitar 
705 - SongFlute 
706 - NewCeleste 
707 - PianoBells 
708 - MellowHorn 
709 - MilkyWays 
710 - Xylo-Brass 
711 - Rosin-Bows 
712 - WireStrung 
713 - DblTrumpet 
714 - FullTines 
715 - Harpsi-Box 
716 - }HardBass{ 
717 - PianoForte 
718 - Good-Vibes 
719 - Analog-DX7 
720 - SpitFlute 
721 - St.Elmo's 
722 - Carolines 
723 - HarpStrum 
724 - ClaviPluck 
725 - MalletHorn 
726 - RaspySax 
727 - HollowHits 
728 - Hard~Tines 
729 - FunkyPluck 
730 - Whap-Synth 
731 - SoloTrumpt 
732 - ReverbStrg 
733 - TouchOrg-1 
734 - TouchOrg-2 
735 - FM-Growth 
736 - ClaviStuff 
737 - EchoMallet 
738 - SteelCans 
739 - TackyPiano 
740 - PlukGuitar 
741 - HighStrung 
742 - ChimeTines 
743 - SoftFrench 
744 - CongaClave 
745 - Clave-Drum 
746 - WoodSimmon 
747 - CastePiano 
748 - PianoFlute 
749 - NeWaveOrgn 
750 - ChorusRing 
751 - EmerSynth 
752 - SoftPiano 
753 - Tine~Brass 
754 - Orgn~Brass 
755 - BCTrumpet 
756 - Xylophonk 
757 - SassyBrass 
758 - ComicSiren 
759 - Bugs&Birds 
760 - RainDrops 
761 - Speak-One 
762 - Speak-Two 
763 - Speak-Thre 
764 - Speak-Four 
765 - What?One? 
766 - FM~Teapot 
767 - TapeRewind 
768 - DX-PIANO 
769 - DX-CP 
770 - METAL PNO. 
771 - CEMBABELL 
772 - CEMBALINE 
773 - CLATRON 
774 - GLAVINET 
775 - ORGAN.B-52 
776 - ORGAN B-29 
777 - ORGATORON 
778 - SYNTH.BELL 
779 - GLASSPHONE 
780 - METAL-GLOC 
781 - SYN.HARP 
782 - GLOCK-BOX 
783 - SYNTH CLIC 
784 - JUICE HARP 
785 - BELLCHIME 
786 - THUMHAPICA 
787 - XYLO-HARP 
788 - METARIMBA 
789 - DIST.GUIT 
790 - 8VER-GUIT 
791 - CLAGUITAR 
792 - SAMP.CONGA 
793 - MARIM-BASS 
794 - PICKIN-W.B 
795 - METALBASS 
796 - IRON BASS 
797 - CLIC-BASS 
798 - SYN.BASS A 
799 - SYN.BASS B 
800 - ANDESFLUTE 
801 - HOLE-FLUTE 
802 - WIND-FLUTE 
803 - BAMBOO CLA 
804 - WIND-BRASS 
805 - HARD BRASS 
806 - BUSSY BRAS 
807 - BRASTATION 
808 - SOFT BRASS 
809 - TRP.-TRB. 
810 - SOFT HORN 
811 - SYN.HORN 
812 - SYNTHPHONE 
813 - SOLO TROMB 
814 - DX-TROMBA 
815 - D.QUART-ST 
816 - A-TOUCH-ST 
817 - SOFT STRGS 
818 - NORMAL STG 
819 - ANALOG-STG 
820 - ORCH-STRGS 
821 - LOWERSTRGS 
822 - SOFT CHOIR 
823 - WHISP-CHOR 
824 - SOLO SYNTH 
825 - IRON TIMPA 
826 - METAL TOM 
827 - NOISY GLAS 
828 - SNARE DRUM 
829 - GATE REVER 
830 - UP UP AWAY 
831 - ANGEL FALL 
832 - {KC}Piano\ 
833 - ElecGrand\ 
834 - EP DlyStr\ 
835 - VicRhodes\ 
836 - KC Tynot \ 
837 - KC Ovrkil\ 
838 - EpSqurMel\ 
839 - WurliChrs\ 
840 - EP.Dstrt \ 
841 - BsEPiano8\ 
842 - EP.SawBel\ 
843 - EpDstrt *\ 
844 - KC EpHwrd\ 
845 - WoodBalls\ 
846 - XyloHrdWd\ 
847 - TorimLong\ 
848 - Grinder 4\ 
849 - Koto\\\\\1 
850 - SoftGuitr\ 
851 - NylonPck3\ 
852 - NylonBel3\ 
853 - Sitar KC \ 
854 - Chroma \ 
855 - Clavi 1 \ 
856 - SqrThang2\ 
857 - KillrD's5\ 
858 - KilerD's2\ 
859 - Bello 3~ \ 
860 - Belashun \ 
861 - RodeBell \ 
862 - ChinezBls\ 
863 - ElecBass*\ 
864 - SoftEgStr\ 
865 - Anna Str \ 
866 - Xylo Str1\ 
867 - Digi Pad \ 
868 - AcSweep 2\ 
869 - PedSweep2\ 
870 - StrBrsPed\ 
871 - TytPhaz//\ 
872 - ObieBrs5/\ 
873 - Anna Fat \ 
874 - SaintWho \ 
875 - Anna Hit1\ 
876 - Anna Hit2\ 
877 - SmoothSqr\ 
878 - Sacuru \ 
879 - SwishBell\ 
880 - Anna Xylo\ 
881 - AcSmack 2\ 
882 - AnnaNasty\ 
883 - SubHarmLd\ 
884 - Bel Lead \ 
885 - LeadEcho1\ 
886 - HrmnicaLd\ 
887 - PennyFlut\ 
888 - KC FLute4\ 
889 - Dbl Reed \ 
890 - TenrReed1\ 
891 - 3WySplt 1\ 
892 - EchoEcho1\ 
893 - EchoEcho2\ 
894 - TapeRewnd2 
895 - A - -{440} 
896 - Soft Spot\ 
897 - Nu Island\ 
898 - NuIsland2\ 
899 - MarimBahh\ 
900 - SftRimPlk\ 
901 - RodeRasp1\ 
902 - E.Clique1\ 
903 - E.Clique2\ 
904 - E.Clique3\ 
905 - E.Clique4\ 
906 - BassXylo1\ 
907 - AcustcSt1\ 
908 - AcustcSt2\ 
909 - AcustcSt3\ 
910 - NoBoKuto1\ 
911 - MutronClv\ 
912 - Clavinet2\ 
913 - ClavSwep1\ 
914 - ClavSwep2\ 
915 - Screamer1\ 
916 - LdSubHrm2\ 
917 - BsE.Tack5\ 
918 - BsE.Tack7\ 
919 - NuMdlXprs\ 
920 - DigiXylo1\ 
921 - Jamaica 2\ 
922 - NewCelest\ 
923 - Glocker 1\ 
924 - MuzacBox \ 
925 - RodeBell2\ 
926 - XprmntlBel 
927 - Long Gong\ 
928 - NogoKoto1\ 
929 - DigiTar 1\ 
930 - SoftElPno\ 
931 - Fuhppps!2\ 
932 - DblReed 2\ 
933 - Flutos 5\ 
934 - OB Geni 2\ 
935 - DX Additv\ 
936 - ChrchMows\ 
937 - Chellezt1\ 
938 - AgiFunki1\ 
939 - AgiStop 1\ 
940 - StrEnsmbl\ 
941 - StrPd7%%%\ 
942 - StrSweep \ 
943 - SmrNParee\ 
944 - Digi Pad \ 
945 - Pad 16 Ft\ 
946 - Brs 16Ft3\ 
947 - Brs 16Ft4\ 
948 - Trompet 1\ 
949 - Ensemble2\ 
950 - Trombone2\ 
951 - FM Nazel1\ 
952 - PdlSweep1\ 
953 - PdlSweep2\ 
954 - BetMeHrDr2 
955 - BetMeHrDr3 
956 - DesrtSean\ 
957 - Vrbration2 
958 - DropVolum\ 
959 - LaserFall\ 
960 - STARTXXX 
961 - O-F-SEQ 1 
962 - O-F-SEQ 2 
963 - P.EGVOICE1 
964 - P.EGVOICE2 
965 - P.EGBRASS1 
966 - P.EGBRASS2 
967 - CHINA S&H1 
968 - CHINA S&H2 
969 - BRASZANCE1 
970 - BRASZANCE2 
971 - MILTONE 
972 - REFLECTION 
973 - FANTASY 
974 - PACHICHORD 
975 - NO.40 
976 - EP-UNIT 
977 - BRASS-UNIT 
978 - GRACE STRG 
979 - GRACEVOICE 
980 - SANCTUS 
981 - DIGI WOW 
982 - DIGI LOG 
983 - BALI GONG 
984 - KUTABEACH 
985 - MECHA KOTO 
986 - MICHELLE 
987 - ANTONIO 
988 - LONG HAIR 
989 - SUS4 STRG. 
990 - SPACE-TRIP 
991 - SPACECHIME 
992 - OPENING 
993 - HITPAD 1 
994 - HITPAD 2 
995 - CLICK BASS 
996 - SLAP BASS 
997 - MUTE BASS 
998 - PICKED GT. 
999 - MUTELUTE 
1000 - THE DUCK 
1001 - BOULOUGNE 
1002 - VIBE-COIN 
1003 - AIR-XYL. 
1004 - PEKINGREED 
1005 - CROSSING 
1006 - DUTWAH 
1007 - REGGAE 
1008 - WOOD GONG1 
1009 - WOOD GONG2 
1010 - WOOD GONG3 
1011 - HEXAGON 1 
1012 - HEXAGON 2 
1013 - HEXAGON 3 
1014 - HEXA-B-DR. 
1015 - HEXA-GLASS 
1016 - GLOW BRASS 
1017 - OLDIES 
1018 - MUSIC BOX 
1019 - EG BIAS 
1020 - NERVYHORNS 
1021 - HAMMER 
1022 - TEQUILA 
1023 - STARTREK 
                */
            } else if (input[1] == 'p') {
                // show partial patch list
                puts("# partials patches -> wX");
                puts("0 G1            8 BCS LGF  E1");
                puts("1 CL SHCI A3    9 DBL HARM G3");
                puts("2 PLATE GONG B 10 VIS LGF#A2");
                puts("3 BNS SUST  E3 11 ANGLECHOIR-C");
                puts("4 VI ISUMP2A   12 OVATION D3");
                puts("5 CH.ORGAN D 3 13 APND.TBL 2");
                puts("6 G2           14 FH CRSC B1");
                puts("7 EHS LG F  F2 15 CHIME E5  -L");
                puts("8 BCS LGF  E1  16 FLS LG F  A3");
                puts("9 DBL HARM G3");
            } else if (input[1] == 's') {
                // show PCM sample list
                puts("# PCM samples -> w7");
                puts(" 0 808-maraca-D      23 nylon B4         46 marimba F#5");
                puts(" 1 808-kik 4-D       24 windchimes       47 fretnoise");
                puts(" 2 808-snr 4-D       25 clarinet A#5     48 piccolo 1-1");
                puts(" 3 808-snr 7-D       26 synth bass 2-B1  49 std Kick 3");
                puts(" 4 808-snr 10D       27 dx7 strike 3     50 rhodes C5");
                puts(" 5 808-snr 12-D      28 harmnc_e4        51 xylophone F#6");
                puts(" 6 808-c-hat1-D      29 dulci_c4         52 B3 F5");
                puts(" 7 808-o-hat1-D      30 sea shore        53 elec kick2");
                puts(" 8 808-ltom m-D      31 power snare 1    54 low woodblock");
                puts(" 9 808-dryclp-D      32 tom hi           55 clap");
                puts("10 808-cwbell-D      33 flute D5         56 high agogo");
                puts("11 808-cnglo2-D      34 piccolo A#6      57 high woodblock");
                puts("12 808-clave -D      35 violin F6        58 ahh choir C5-L");
                puts("13 temple block 3-mf 36 viola A#3        59 acoustic bass A#3");
                puts("14 mtnr-3 roll P     37 pan flute C6     60 bird");
                puts("15 mtnr-3 hit 4      38 tuba D#4         61 harmonics 2");
                puts("16 58_79_crashchoke1 39 oboe A#4         62 kalimba A5");
                puts("17 msnr-shell 2      40 english horn C5  63 orch hit G#6");
                puts("18 cello E4          41 palmmuted gtr D4 64 harp D#4");
                puts("19 cello-F#2         42 shamisen F#5     65 metronome bell(L)");
                puts("20 steel Gtr B4      43 koto F#5         66 trumpet C4");
                puts("21 clean F50         44 steel drum C6");
                puts("22 synthvz F#5       45 power kick 3");
            }
        } else if (input[0] == ':') {
            if (input[1] == 'w') {
                break;
            }
            if (input[1] == 'q') {
                break;
            }
            if (input[1] == 'c') {
                write(1, clear, strlen(clear));
                write(1, home, strlen(home));
            }
        } else if (input[0] == '%') {
            int16_t *capture = amy_captured();
            int frames = amy_frames();
            cls();
            int i;
            int32_t min = INT16_MAX; // -1; // ???
            int32_t max = INT16_MIN;
            for (i=0; i<frames; i++) {
                min = MIN(capture[i], min);
                max = MAX(capture[i], max);
            }
            int32_t gap = frames/COLS*4;
            if (gap < 0) gap = 1;
            for (i=0; i<frames; i+=gap) {
                int32_t x, z;
                x = map(i, 0, frames, 0, COLS);
                //z = map(0, min, max, 0, ROWS);
                z = map(0, -min, -max, 0, ROWS);
                set(x, z, YELLOW, 0);
            }
            int32_t prev = 0;
#define SIGN(x) (((x)>0)-((x)<0))
            for (i=0; i<frames; i++) {
                int32_t x, y;
                int32_t this = capture[i];
                x = map(i, 0, frames, 0, COLS);
                //y = map(this, min, max, 0, ROWS-1);
                y = map(this, -min, -max, 0, ROWS-1);
                if (this <= INT16_MIN+1) {
                    set(x, y, RED, 1);
                } else if (this >= INT16_MAX-1) {
                    set(x, y, RED, 1);
                } else if (SIGN(prev) != SIGN(this)) {
                    set(x, ROWS/2, BLUE, 1);
                } else {
                    set(x, y, GREEN, 0);
                }
                prev = this;
            }
            double msec = (double)(1000.0/SAMPLE_RATE*frames);
            printf("   AMP s16 [%d,%d] ~> fp [%.06f,%.06f]\n",
                min, max,
                map(min, INT16_MIN+1, INT16_MAX, -1.0, 1.0),
                map(max, INT16_MIN+1, INT16_MAX, -1.0, 1.0));
            char more = 0;
            if (amy_full()) more = 1;
            show(more);
            printf("   CAP frames [0,%d] ~> [0.000,%.03f] msec\n", frames, msec);
        } else if (input[0] == '^') {
            int pid = getpid();
            int epoch = time(NULL);
            int16_t *capture = amy_captured();
            int frames = amy_frames();
            char name[1024] = "";
            if (input[1] == 'w') {
                // mono s16 .WAV @ 44100
                drwav_data_format format;
                drwav wav;
                format.container = drwav_container_riff;  // <-- drwav_container_riff = normal WAV files, drwav_container_w64 = Sony Wave64.
                format.format = DR_WAVE_FORMAT_PCM;       // <-- Any of the DR_WAVE_FORMAT_* codes.
                format.channels = 1;
                format.sampleRate = 44100;
                format.bitsPerSample = 16;
                sprintf(name, "amy.%d.%d.wav", pid, epoch);
                drwav_init_file_write(&wav, name, &format, NULL);
                //...
                drwav_uint64 framesWritten = drwav_write_pcm_frames(&wav, frames, capture);
                printf("# capture %lld frames to %s\n", framesWritten, name);
                drwav_uninit(&wav);
            } else {
                // raw pcm s16 @ 44100
                FILE *out = NULL;
                sprintf(name, "amy.%d.%d.raw", pid, epoch);
                out = fopen(name, "wb+");
                if (out == NULL) {
                    printf("# could not open %s\n", name);
                    out = stdout;
                } else {
                    printf("# capture %d frames to %s\n", frames, name);
                    fwrite(capture, 1, frames * sizeof(int16_t), out);
                    fclose(out);
                }
            }
        } else if ((input[0] == '!') || (input[0] == '=')) {
            FILE *out = stdout;
            int pid = getpid();
            int epoch = time(NULL);
            int frames = amy_frames();
            int16_t *capture = amy_captured();
            if (input[0] == '!') {
                char outfile[1024] = "";
                sprintf(outfile, "amy.%d.%d.json", pid, epoch);
                out = fopen(outfile, "w+");
                if (out == NULL) {
                    printf("# could not open %s\n", outfile);
                    out = stdout;
                } else {
                    printf("# capture %d frames to %s\n", frames, outfile);
                }
            }
            double msec = (double)(1000.0/SAMPLE_RATE*frames);
            int sysclock = amy_sysclock();
            int total = total_samples;
            fprintf(out,
                "{\"pid\":%d, \"epoch\":%d, \"sysclock\":%d, \"total_samples\":%d, \"rate\":%d, \"frames\":%d, \"msec\":%.03f, \"raw\":[",
                pid, epoch, sysclock, total, SAMPLE_RATE, frames, msec);
            if (frames) {
                char c = ',';
                for (int i=0; i<frames; i++) {
                    if (i == (frames-1)) {
                        c = ']';
                    }
                    fprintf(out, "%d%c", capture[i], c);
                }
            } else {
                fprintf(out, "]");
            }
            fprintf(out, "}\n");
            if (out != stdout) {
                if (out != NULL) {
                    fclose(out);
                }
            }
        } else if (input[0] == '@') {
            // see capture level
            int frames = amy_frames();
            double msec = (double)(1000.0/SAMPLE_RATE*frames);
            int sysclock = amy_sysclock();
            int total = total_samples;
            int pid = getpid();
            int epoch = time(NULL);
            printf("{\"pid\":%d, \"epoch\":%d, \"sysclock\":%d, \"total_samples\":%d, \"rate\":%d, \"frames\":%d, \"msec\":%.03f}\n",
                pid, epoch, sysclock, total, SAMPLE_RATE, frames, msec);
        } else if (input[0] == '>') {
            // look for >[0-9]* for capture size
            int max = 0;
            int offset = 1;
            char *c = input;
            if (c[offset] >= '0' && c[offset] <= '9') {
                while (c[offset]) {
                    if (c[offset] >= '0' && c[offset] <= '9') {
                        int n = c[offset] - '0';
                        max *= 10;
                        max += n;
                    } else {
                        process_wire = offset;
                        // not a number, stop doing thing
                        break;
                    }
                    offset++;
                }
            }
            // start capture
            //double x = (double)max;
            double y = ceil((double)max/ms_per_block);
            int z = (int)y;
            if (z <= 0) z = 0;
            if (z >= BLOCK_COUNT) z = BLOCK_COUNT;
            if (verbose) {
                printf("# capturing %d blocks -> %d frames -> %.03f msec\n", z, z*BLOCK_SIZE, z*ms_per_block);
            }
            amy_enable_capture(1, z * BLOCK_SIZE);
        } else if (input[0] == '<') {
            // stop capture
            amy_enable_capture(0, 0);
            if (verbose) {
                printf("# capture stopped\n");
            }
        } else if (input[0] == '*') {
            double a0_max = (double)INT16_MIN;
            double s16_max = (double)(INT16_MAX);
            int16_t *capture = amy_captured();
            int length = amy_frames();
            int i;
            // maximize capture buffer dynamic range
            for (i=0; i<length; i++) {
                double n = (double)capture[i];
                a0_max = MAX(a0_max, n);
            }
            double factor = s16_max / a0_max;
            printf("# a0_max=%.02f, s16_max=%.02f, factor=%.04f\n",
                a0_max, s16_max, factor);
            // multiply each value by the scaling factor
            for (i=0; i<length; i++) {
                double sample = (double)capture[i];
                double n = (sample * factor);
                if (n > (double)INT16_MAX) {
                    n = (double)INT16_MAX;
                } else if (n < (double)(INT16_MIN+1)) {
                    n = (double)INT16_MIN+1;
                }
                capture[i] = (int16_t)(n);
            }
        } else if (input[0] == '~') {
            // scan capture
            int16_t *capture = amy_captured();
            int i;
            int top = 0;
            int end = amy_frames();
            //int max = end;
            int len = 0;
            int flag = 0;
            for (i=0; i<end; i++) {
                if (capture[i] != 0) {
                    top = i-1;
                    if (top < 0) top = 0;
                    flag = 1;
                    break;
                }
            }
            if (flag) {
                for (i=end-1; i>=top; i--) {
                    if (capture[i] != 0) {
                        end = i+1;
                        break;
                    }
                }
                len = end - top + 1;
            } else {
                end = 0;
                len = 0;
            }
            char r = amy_trim(len);
            printf("{\"begin\":%d, \"end\":%d, \"length\":%d, \"trimmed\": %d}\n",
                top, end, len, r);
        } else if (input[0] == '(') {
            int patch = 67;
            int length = 0;
            int offset = 0;
            int16_t *capture = amy_captured();
            if ((input[1] >= '0') && (input[1] <= '9')) {
                patch = input[1] - '0';
                if ((input[2] >= '0') && (input[2] <= '9')) {
                    patch *= 10;
                    patch += input[2] - '0';
                }
            }
            offset = pcm_map[patch].offset;
            length = pcm_map[patch].length;
#if 1
            int i;
            int j = 0;
            int16_t *sample = &pcm[offset];
            printf("# stretch %d -> %d frames from patch %d\n",
                length, length*2, patch);
            for (i=0; i<length; i++) {
                capture[j++] = sample[i];
                capture[j++] = sample[i];
            }
            length *= 2;
#else
            printf("# capture %d frames from patch %d\n",
                length, patch);
            memcpy(
                capture,
                &pcm[offset],
                length*sizeof(int16_t)
            );
#endif
            amy_set_frames(length);
        } else if (input[0] == ')') {
            // capture to user pcm

            int patch = 67;
            int length = 0;
            int offset = PCM_LENGTH;
            int16_t *capture = amy_captured();
            length = amy_frames();
#if 1
            printf("# crunch from %d -> %d frames into patch %d\n",
                length, length/2, patch);
            // crunch data by skipping every sample
            // might improve by averaging two samples?
            int i;
            int j = 0;
            int16_t *sample = &pcm[offset];
            for (i=0; i<length; i+=2) {
                sample[j] = capture[i];
                j++;
            }
            length /= 2;
#else
            printf("# capture %d frames to patch %d\n",
                length, patch);
            // leave data as is... since PCM samples are 22050, this makes the duration 2x
            memcpy(&pcm[offset], capture, length*sizeof(int16_t));
#endif
            pcm_map[PCM_SAMPLES].length = length;
            pcm_map[PCM_SAMPLES].loopstart = 0;
            pcm_map[PCM_SAMPLES].loopend = length-1;
            pcm_map[PCM_SAMPLES].midinote = 69;
        } else if (input[0] == '#') {
            // don't do anything...
        } else {
            process_wire = 0;
        }
        
        if (process_wire >= 0) {
            t0 = amy_sysclock();
            // pre-process input
            char *here = input + process_wire;
            // printf("# process_wire @ [%d] '%s'\n", process_wire, here);
            int mptr = 0;
            int cptr = 0;
            int state = 0;
            // look for +[0-9]* for "now + number ms"
            // look for ;
            int delta_ms = 0;
            now = amy_sysclock();
            int i;
            while (1) {
                char c = *here;
                if (state == 1) {
                    // inside relative-timestamp number scan...
                    if (c >= '0' && c <= '9') {
                        delta_ms = (delta_ms * 10) + (c-'0');
                    } else if (c == '\0') {
                        printf("# no message to timestamp\n");
                        break;
                    } else {
                        sprintf(timestamp, "t%d", now + delta_ms);
                        for (i=0; i<strlen(timestamp); i++) {
                            messages[mptr][cptr] = timestamp[i];
                            cptr++;
                        }
                        messages[mptr][cptr] = '\0';
                        state = 0;
                    }
                }
                if (state == 0) {
                    if (c == '\0') {
                        messages[mptr][cptr] = '\0';
                        break;
                    } else if (c == '+') {
                        delta_ms = 0;
                        state = 1;
                    } else if (c == '#') {
                        messages[mptr][cptr] = '\0';
                        *here = '\0';
                        break;
                    } else if (c == ';') {
                        *here = '\0';
                        messages[mptr][cptr] = '\0';
                        cptr = 0;
                        mptr++;
                    } else if (c == '\n') {
                        // skip
                    } else if (c == '\r') {
                        // skip
                    } else if (c == '\t') {
                        // skip
                    } else if (c == ' ') {
                        // skip
                    } else {
                        messages[mptr][cptr++] = c;
                    }
                }
                here++;
            }
            t1 = amy_sysclock();
            for (i=0; i<=mptr; i++) {
                if (strlen(messages[i]) > 0) {
                    char fix[LEN_MESSAGES * 2];
                    if (messages[i][0] != 't') {
                        sprintf(fix, "t%d%s", now, messages[i]);
                    } else {
                        sprintf(fix, "%s", messages[i]);
                    }
                    if (verbose) {
                        printf("# AMY #%d <- '%s'\n", i, fix);
                    }
                    amy_play_message(fix);
                }
            }
            if (verbose) {
                printf("# T1-T0 = %d\n", t1-t0);
            }
        }
        free(input);
    }
    
    amy_live_stop();

    return 0;
}

