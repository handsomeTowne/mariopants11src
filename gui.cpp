// gui.cpp
//   some simple GUI elements

#include <cstdlib> // for NULL
#include "gui.h"
#include "os.h"

namespace gui
{

Bob* focus = NULL;

// Bob

Bob::Bob(int x_, int y_, int w_, int h_, bool capture_)
{
	x = x_;
	y = y_;
	w = w_;
	h = h_;
	x2 = x + w;
	y2 = y + h;
	down = false;
	capture = capture_;
}

bool Bob::bound(int x_, int y_)
{
	return x_ >= x && x_ < x2 && y_ >= y && y_ < y2;
}

void Bob::mouse_down(int mx, int my)
{
	if (capture)
	{
		down = true;
		focus = this;
	}
	else
	{
		click(mx-x,my-y);
	}
}

void Bob::mouse_move(int mx, int my)
{
	if (capture)
		down = bound(mx,my);
}

bool Bob::update(int ms)
{
	return false;
}

void Bob::mouse_up(int mx, int my)
{
	down = false;
	focus = NULL;
	if (capture && bound(mx,my))
		click(mx-x,my-y);
}

void Bob::draw()
{
}

// action when Bob is clicked, x/y are relative to Bob
void Bob::click(int mx, int my) {}

// Bobslider

BobSlider::BobSlider(int x_, int y_, int w_, int h_,
	                    int min_, int max_, int edge_, int* pos_) :
	Bob(x_,y_,w_,h_,true)
{
	set_range(min_,max_,edge_);
	pos = pos_;
}

void BobSlider::set_range(int min_, int max_, int edge_)
{
	min = min_;
	max = max_;
	edge = edge_;
	range = w - (2 * edge);
}

void BobSlider::mouse_down(int mx, int my)
{
	Bob::mouse_down(mx,my);
	click(mx-x,my-y);
}

void BobSlider::mouse_move(int mx, int my)
{
	Bob::mouse_move(mx,my);
	click(mx-x,my-y);
}

void BobSlider::click(int mx, int my)
{
	mx = mx - edge;
	*pos = min + ((mx * max) / range);
	if (*pos < min) *pos = min;
	if (*pos > max) *pos = max;
}

// BobButton

BobButton::BobButton(int x_, int y_, int w_, int h_,
	        int icon_, int ix_, int iy_, void (*action_)()) :
	Bob(x_,y_,w_,h_,true)
{
	icon = icon_;
	ix = x + ix_;
	iy = y + iy_;
	action = action_;
}

void BobButton::click(int mx, int my)
{
	if(action) action();
}

void BobButton::draw()
{
	if(down)
		os::draw_icon(ix,iy,icon);
}

// BobRepeat

BobRepeat::BobRepeat(int x_, int y_, int w_, int h_,
                     void (*action_)(), int wait_, int rep_) :
	Bob(x_,y_,w_,h_,true)
{
	action = action_;
	wait = wait_;
	rep = rep_;
}

void BobRepeat::mouse_down(int mx, int my)
{
	Bob::mouse_down(mx,my);
	if (action) action();
	dt = wait;
}

void BobRepeat::mouse_up(int mx, int my)
{
	down = false;
	focus = NULL;
	// no action
}

bool BobRepeat::update(int ms)
{
	// continue action every time rep threshold is crossed
	// return true if redraw needed
	dt -= ms;
	if (dt < 0)
	{
		dt = rep;
		if (action) action();
		return true;
	}
	return false;
}

// BobPoke

BobPoke::BobPoke(int x_, int y_, int w_, int h_, void (*action_)()) :
	Bob(x_,y_,w_,h_,false)
{
	action = action_;
}

void BobPoke::click(int mx, int my)
{
	if(action) action();
}

// BobPoker

BobPoker::BobPoker(int x_, int y_, int w_, int h_, void (*action_)(int,int)) :
	Bob(x_,y_,w_,h_,false)
{
	action = action_;
}

void BobPoker::click(int mx, int my)
{
	if(action) action(mx,my);
}

// BobPorker

BobPorker::BobPorker(int x_, int y_, int w_, int h_, void (*action_)(int,int)) :
	Bob(x_,y_,w_,h_,false)
{
	action = action_;
}

void BobPorker::click(int mx, int my)
{
	if(action) action(mx+x,my+y);
}

} // namespace gui

// end of file
