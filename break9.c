#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <mouse.h>
#include <keyboard.h>

enum {
	Ninterval = 20,
	Width = 800,
	Height = 600,
	Ncellsize = 18,
	Nblockwidth = 6,
	Nblockheight = 2,
	Nblockoffset = 1,
	Nblockoffseth = .5,
	Ntxtoffset = 1.5,
	Nblockrows = 4,
	Nplayerwidth = 7,
};

#define PLAYER_SIZE 6
#define BLOCK_GAP 4
#define BLOCK_ROWS 5
#define BLOCK_OFFSET 0

#define CLAMP(x,min,max) (x)<(min)?(min):(x)>(max)?(max):(x)
#define ptstorect(p1,p2) (Rectangle)(Rect(p1.x,p1.y,p1.x+p2.x,p1.y+p2.y))

#define ballsize (int)(Ncellsize/2)
#define gridwidth (int)(Dx(screen->r))/(Ncellsize)
#define gridheight (int)(Dy(screen->r))/(Ncellsize)
#define Cell(n) (int)(n * Ncellsize)

typedef struct block Block;
struct block
{
	Rectangle r;
	int health;
};

typedef struct ball Ball;
struct ball
{
	Point pt;
	int dx, dy;
};

Ball ball;
Rectangle player;
Block *blocks;
int running, score;
char buf[64];

Mousectl *mctl;
Keyboardctl *kctl;
Channel *drawc;

Image *bg, *fg, *playerfg, *playerborderfg, *ballfg, *blockfg, *blockborderfg;

void
redraw(void)
{
	int i;

	draw(screen, screen->r, bg, nil, ZP);
	string(screen, addpt(screen->r.min, Pt(
			Dx(screen->r)/2 - (stringwidth(display->defaultfont, buf)/2)
			, display->defaultfont->height / 2)),
		display->black, ZP, display->defaultfont, buf);

	// draw blocks
	for(i=0; i<(Nblockrows * (gridwidth / Nblockwidth)); i++){
		if(!blocks[i].health) continue;
		draw(screen, rectaddpt(blocks[i].r, screen->r.min), blockfg, nil, ZP);
		border(screen, rectaddpt(blocks[i].r, screen->r.min), 2, blockborderfg, ZP);
	}

	// draw ball
	fillellipse(screen, addpt(screen->r.min, ball.pt),
		ballsize, ballsize, ballfg, ZP);

	// draw player
	draw(screen, rectaddpt(player, screen->r.min), playerfg, nil, ZP);
	border(screen, rectaddpt(player, screen->r.min), 1, playerborderfg, ZP);

	flushimage(display, 1);
}

void
resizeproc(void*)
{
	threadsetname("resizeproc");
	for(;;){
		recvul(mctl->resizec);
		if (getwindow(display, Refnone) < 0)
			sysfatal("getwindow: %r");
		redraw();
	}
}

void
kbdproc(void*)
{
	Rune r;

	threadsetname("kbdproc");
	for(;;){
		recv(kctl->c, &r);
		switch(r){
		case 'p':
			running =!running;
			break;
		case 'q':
		case Kdel:
			free(blocks);
			closemouse(mctl);
			closekeyboard(kctl);
			closedisplay(display);
			threadexitsall(nil);
			break;
		case 'h':
		case Kleft:
			if(!running) return;
			player.min.x = CLAMP(player.min.x - Cell(1), 0, Cell(gridwidth));
			player.max.x = CLAMP(player.max.x - Cell(1), Cell(Nplayerwidth), Cell(gridwidth));
			break;
		case 'l':
		case Kright:
			if(!running) return;
			player.min.x = CLAMP(player.min.x + Cell(1), 0, Cell(gridwidth)-Cell(Nplayerwidth));
			player.max.x = CLAMP(player.max.x + Cell(1), 0, Cell(gridwidth));
			break;
		}
	}
}

void
timerproc(void *)
{
	threadsetname("timerproc");
	for(;;){
		sleep(Ninterval);
		sendul(drawc, 0);
		redraw();
	}
}

void
gamethread(void*)
{
	int i;

	threadsetname("gameproc");
	for(;;){
		recvul(drawc);
		if(running){

			// update ball
			if(ball.pt.x-ballsize <= 0 || ball.pt.x >= Cell(gridwidth))
				ball.dx *= -1;
			if(ball.pt.y-ballsize <= 0 || ball.pt.y >= Cell(gridheight))
				ball.dy *= -1;
			ball.pt = addpt(ball.pt, Pt(Cell(ball.dx)/2, Cell(ball.dy)/2));

			// check player collision
			if(ptinrect(ball.pt, player)){
				ball.pt.y -= ballsize;
				ball.dy *= -1;
			}

			for(i=0; i<Nblockrows*(gridwidth/Nblockwidth); i++){
				if(ptinrect(ball.pt, blocks[i].r) && blocks[i].health){
					blocks[i].health--;
					ball.pt.y += Cell(1);
					ball.dy *= -1;
					if(!blocks[i].health)
						score++;
					snprint(buf, sizeof buf, "%d point(s)", score);
				}
			}

			// check death
			if(ball.pt.y > player.max.y){
				snprint(buf, sizeof buf, "you lost buddy, %d point(s)", score);
				running = 0;
			}
		}
		redraw();
	}
}

void
resize(int x, int y)
{
	int fd;

	if((fd = open("/dev/wctl", OWRITE))){
		fprint(fd, "resize -dx %d -dy %d", x, y);
		close(fd);
	}
}

void
initgame(void)
{
	Point pt;
	int i;

	ball.pt = Pt(Cell(gridwidth / 2), Cell(gridheight / 2));
	ball.dx = ball.dy = -1;

	pt = Pt(Cell(gridwidth / 2), Cell(gridheight));
	player = Rect(
		pt.x - Cell(Nplayerwidth/2),
		pt.y - Cell(2),
		pt.x + Cell(Nplayerwidth/2),
		pt.y - Cell(1)
	);

	if((blocks = malloc(sizeof(*blocks) * (Nblockrows*(gridwidth/Nblockwidth)))) == nil)
		sysfatal("malloc: %r");

	for(i=0; i<Nblockrows*(gridwidth/Nblockwidth); i++){
		pt = Pt(
			Cell(Nblockoffset) + ((i % (gridwidth / Nblockwidth)) * Cell(Nblockwidth)),
			Cell(Ntxtoffset) + Cell(Nblockoffseth) + ((i / (gridwidth / Nblockwidth)) * Cell(Nblockheight))
		);
		blocks[i].r = Rect(
			pt.x,
			pt.y + Cell(Ntxtoffset),
			pt.x + Cell(Nblockwidth) - (Cell(Nblockoffset) / 2),
			pt.y + Cell(Nblockheight) + Cell(Ntxtoffset) - Cell(Nblockoffseth)
		);
		blocks[i].health = 1;
	}

	running = 1;
}

void
loadtheme(void)
{
	// TODO: read from /dev/theme
	bg = allocimagemix(display, DPalebluegreen, DWhite);
	fg = allocimage(display, Rect(0, 0, 1, 1), RGB24, 1, DBlack);
	playerfg = allocimage(display, Rect(0,0,1,1), RGB24, 1, 0x7DFDA4FF);
	playerborderfg = allocimage(display, Rect(0,0,1,1), RGB24, 1, 0x101010FF);
	ballfg = allocimage(display, Rect(0,0,1,1), RGB24, 1, 0xE47674FF);
	blockfg = allocimage(display, Rect(0,0,1,1), RGB24, 1, 0x72DEC2FF);
	blockborderfg = allocimage(display, Rect(0,0,1,1), RGB24, 1, 0x555555FF);
}

void
threadmain(int, char *[])
{
	if(initdraw(nil, nil, argv0) < 0)
		sysfatal("initdraw: %r");
	if((mctl = initmouse(nil, nil)) == nil)
		sysfatal("initmouse: %r");
	if((kctl = initkeyboard(nil)) == nil)
		sysfatal("initkeyboard: %r");
	drawc = chancreate(sizeof(ulong), 0);

	resize(Width, Height);

	loadtheme();
	initgame();

	proccreate(timerproc, nil, 8*1024);
	threadcreate(resizeproc, nil, 8*1024);
	threadcreate(kbdproc, nil, 8*1024);
	threadcreate(gamethread, nil, 8*1024);

	threadexits(nil);
}
