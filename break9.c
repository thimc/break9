#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <mouse.h>
#include <keyboard.h>

#define INTERVAL 40

#define WIDTH 800
#define HEIGHT 600

#define CELL_DIV 36
#define OFFSET_X 1
#define OFFSET_Y 1
#define PLAYER_SIZE 8
#define BLOCK_SIZE 3
#define BLOCK_GAP 1
#define BLOCK_ROWS 5
#define BLOCK_OFFSET 0

#define CLAMP(x,min,max) (x)<(min)?(min):(x)>(max)?(max):(x)

#define scrx (int)(screen->r.min.x)
#define scry (int)(screen->r.min.y)
#define scrmx (int)(screen->r.max.x)
#define scrmy (int)(screen->r.max.y)
#define scrw (int)(scrmx - scrx)
#define scrh (int)(scrmy - scry)

#define cell_size (int)(scrw)/(int)CELL_DIV
#define ball_size (int)cell_size
#define grw (int)(scrw)/(cell_size)
#define grh (int)(scrh)/(cell_size)

typedef struct block Block;
struct block
{
	int x, y, w, h, health;
};

typedef struct player Player;
struct player
{
	int x, y;
};

typedef struct ball Ball;
struct ball
{
	int x, y, dx, dy;
};

char *menustr[] = { "pause", "exit", nil };
Menu menu = { menustr };

Ball ball;
Player player;
Block *blocks;
int running;

Mousectl *mctl;
Keyboardctl *kctl;

Image *bg, *fg, *playerfg, *playerborderfg, *ballfg, *blockfg, *blockborderfg;

char buf[64] = {0};

void draw_block(Block, Image*);
void draw_player(int, int);
void updateball(void);
void checkcollision(void);
void draw_ball(int, int);


void
draw_block(Block b, Image *col)
{
	Rectangle r = Rect(scrx + (b.x * cell_size) + BLOCK_GAP,
		scry + (b.y * cell_size) + (b.h * cell_size),
		scrx + (b.x * cell_size) - BLOCK_GAP + (b.w * cell_size),
		scry + (b.y * cell_size) + cell_size - BLOCK_GAP + (b.h * cell_size));

	draw(screen, r, col, nil, ZP);
	border(screen, r, 2, blockborderfg, ZP);
}

void
draw_player(int x, int y)
{
	Rectangle r = Rect(scrx + (x * cell_size) + BLOCK_GAP,
		scry + (y * cell_size) + (cell_size / 2),
		scrx + (x * cell_size) - BLOCK_GAP + (PLAYER_SIZE * cell_size),
		scry + (y * cell_size) - BLOCK_GAP + (cell_size * 1.25));

	draw(screen, r, playerfg, nil, ZP);
	border(screen, r, 1, playerborderfg, ZP);
}

void
updateball(void)
{
	if(ball.x<=0||ball.x>=grw-1)
		ball.dx *= -1;
	if(ball.y<=0||ball.y>=grh)
		ball.dy *= -1;

	ball.x += ball.dx;
	ball.y += ball.dy;
}

void
checkcollision(void)
{
	int i;
	snprint(buf, sizeof buf, " ");

	if((ball.y >= player.y && ball.y  <= player.y + PLAYER_SIZE)
		&& (ball.x > player.x && ball.x <= player.x + PLAYER_SIZE)){
		snprint(buf, sizeof buf, "!");
		ball.y -= 2;
		ball.dy *= -1;
	}

	for(i=0; i<BLOCK_ROWS*grw; i++){
		if((ball.y-1 >= blocks[i].y && ball.y-1 <= blocks[i].y + blocks[i].h)
			&& (ball.x-1 > blocks[i].x && ball.x-1 <= blocks[i].x + blocks[i].w)){
			if(!blocks[i].health) break;

			blocks[i].health = 0;
			ball.y += 1;
			ball.dy *= -1;
			break;
		}
	}

}

void
draw_ball(int x, int y)
{
	fillellipse(screen, Pt(scrx + (x * cell_size) + (ball_size / 2),
		scry + (y * cell_size) + (ball_size / 2)),
		ball_size/2, ball_size/2, blockfg, ZP);
}

void
redraw(void)
{
	int i, j;

	draw(screen, screen->r, bg, nil, ZP);

	if(!running){
		snprint(buf, sizeof buf, "<paused>");
	}

	string(screen,
		addpt(Pt(scrx, scry),
		Pt((scrw/2)-(stringwidth(display->defaultfont, buf)/2), 10)),
		fg, ZP, display->defaultfont, buf);


	for(i=0; i<BLOCK_ROWS; i++){
		for(j=0; j<grw; j++){
			if(blocks[(j*BLOCK_ROWS)+i].health > 0)
				draw_block(blocks[(j*BLOCK_ROWS)+i], ballfg);
		}
	}

	draw_player(player.x, player.y);
	draw_ball(ball.x, ball.y);

	flushimage(display, 1);
}

void
eresize(void)
{
	player.y = grh-2;
	redraw();
}

void
emouse(Mouse *m)
{
	if(m->buttons != 4)
		return;
	switch(menuhit(3, mctl, &menu, nil)){
	case 0:
		running = !running;
		if(running)
			sprint(menustr[0], "pause");
		else
			sprint(menustr[0], "play");
		break;
	case 1:
		threadexitsall(nil);
	}
}

void
ekeyboard(Rune k)
{
	if(!running) return;
	switch(k){
	case 'q':
	case Kdel:
		free(blocks);
		threadexitsall(nil);
		break;
	case 'h':
	case Kleft:
		player.x = CLAMP(player.x-1,0,grw);
		break;
	case 'l':
	case Kright:
		player.x = CLAMP(player.x+1,0,grw-PLAYER_SIZE);	
		break;
	}
}

void
usage(void)
{
	fprint(2, "usage: %s\n", argv0);
	threadexitsall("usage");
}

void
timerproc(void *c)
{
	threadsetname("timer");
	for(;;){
		sleep(INTERVAL);
		sendul(c, 0);
	}
}

void
threadmain(int argc, char *argv[])
{
	enum { Emouse, Eresize, Ekeyboard, Etimer };
	Mouse m;
	Rune k;
	Alt a[] = {
		{ nil, &m,  CHANRCV },
		{ nil, nil, CHANRCV },
		{ nil, &k,  CHANRCV },
		{ nil, nil, CHANRCV },
		{ nil, nil, CHANEND },
	};
	int n, i, j;

	ARGBEGIN{ }ARGEND;

	if(initdraw(nil, nil, argv0) < 0)
		sysfatal("initdraw: %r");
	if((mctl = initmouse(nil, screen)) == nil)
		sysfatal("initmouse: %r");
	if((kctl = initkeyboard(nil)) == nil)
		sysfatal("initkeyboard: %r");

	a[Emouse].c = mctl->c;
	a[Eresize].c = mctl->resizec;
	a[Ekeyboard].c = kctl->c;
	a[Etimer].c = chancreate(sizeof(ulong), 0);
	proccreate(timerproc, a[Etimer].c, 2048);

	// TODO: read from /dev/theme
	bg = allocimagemix(display, DPalebluegreen, DWhite);
	fg = allocimage(display, Rect(0, 0, 1, 1), RGB24, 1, DBlack);
	playerfg = allocimage(display, Rect(0,0,1,1), RGB24, 1, 0x7DFDA4FF);
	playerborderfg = allocimage(display, Rect(0,0,1,1), RGB24, 1, 0x101010FF);
	ballfg = allocimage(display, Rect(0,0,1,1), RGB24, 1, 0xE47674FF);
	blockfg = allocimage(display, Rect(0,0,1,1), RGB24, 1, 0x72DEC2FF);
	blockborderfg = allocimage(display, Rect(0,0,1,1), RGB24, 1, 0x555555FF);

	player.x = grw/2-(PLAYER_SIZE/2)-1;

	ball.x = grw/2-1;
	ball.y = grh-4;
	ball.dx = 1;
	ball.dy = 1;

	blocks = malloc(sizeof(*blocks) * (grw * BLOCK_ROWS));
	if(!blocks)
		sysfatal("malloc blocks: %r");

	n = 0;
	for(i=0; i<BLOCK_ROWS; i++){
		for(j=0; j<grw; j++){
			blocks[(j*BLOCK_ROWS)+i].y = i+BLOCK_OFFSET;
			blocks[(j*BLOCK_ROWS)+i].x = n;
			blocks[(j*BLOCK_ROWS)+i].w = BLOCK_SIZE;
			blocks[(j*BLOCK_ROWS)+i].h = 2;
			blocks[(j*BLOCK_ROWS)+i].health = 1; // BLOCK_ROWS-i;
			n=j*BLOCK_SIZE;
		}
	}

	running = 1;
	eresize();
	for(;;){
		switch(alt(a)){
		case Emouse:
			emouse(&m);
			break;
		case Eresize:
			if(getwindow(display, Refnone) < 0)
				sysfatal("getwindow: %r");
			eresize();
			break;
		case Ekeyboard:
			ekeyboard(k);
			redraw();
			break;
		case Etimer:
			if(running){
				updateball();
				checkcollision();
			}
			redraw();
			break;
		}
	}
}