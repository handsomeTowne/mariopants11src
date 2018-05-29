#pragma once

// gui.h
//   some simple GUI elements

namespace gui
{

// Bob base class for GUI elements, a clickable frame that can capture a mouse-down
struct Bob
{
	int x, y, w, h, x2, y2;
	bool down, capture;

	// x,y w,h define a clickable area
	// * if capture is true, click() happens on mouse release if still bounded
	// * if capture is false, click() happens immediately on mouse down
	// override draw() and click() for specific behaviour
	// if capture is true, down keeps track of whether the mouse remains down on a Bob

	Bob(int x_, int y_, int w_, int h_, bool capture_);
	bool bound(int x_, int y_);
	virtual void mouse_down(int mx, int my);
	virtual void mouse_move(int mx, int my); // only when captured
	virtual void mouse_up(int mx, int my);
	virtual bool update(int ms); // only when captured, return true if redraw required
	virtual void draw();
	virtual void click(int mx, int my); // x/y are relative to Bob
};

// BobSlider just updates a position int passed to it
struct BobSlider : public Bob
{
	int min, max, edge, range;
	int* pos;

	BobSlider(int x_, int y_, int w_, int h_,
	          int min_, int max_, int edge_, int* pos_);
	void set_range(int min_, int max_, int edge_);
	virtual void mouse_down(int mx, int my);
	virtual void mouse_move(int mx, int my);
	virtual void click(int mx, int my);
};

// BobButton captures a mouse down, and does click() if mouse up while still on it
// * draws the specified icon if down, at ix/iy offset relative to x/y
struct BobButton : public Bob
{
	int icon, ix, iy;
	void (*action)();

	BobButton(int x_, int y_, int w_, int h_,
	          int icon_, int ix_, int iy_, void (*action_)());
	virtual void click(int mx, int my);
	virtual void draw();
};

// BobRepeat captures, activates action on mouse down, and will repeat click every rep ms after initial wait ms
// * very similar to BobPoke below, but repeats action
struct BobRepeat : public Bob
{
	int icon, ix, iy, wait, rep, dt;
	void (*action)();

	BobRepeat(int x_, int y_, int w_, int h_,
	          void (*action_)(), int wait_, int rep_);

	virtual void mouse_down(int mx, int my);
	virtual void mouse_up(int mx, int my);
	virtual bool update(int ms);
};

// BobPoke clicks on mouse down, activates action, no visual component
struct BobPoke : public Bob
{
	void (*action)();

	BobPoke(int x_, int y_, int w_, int h_, void (*action_)());
	virtual void click(int mx, int my);
};

// BobPoker is like BobPoke but gives the x/y location relative to the frame
struct BobPoker : public Bob
{
	void (*action)(int,int);

	BobPoker(int x_, int y_, int w_, int h_, void (*action_)(int,int));
	virtual void click(int mx, int my);
};


// BobPorker is like BobPoker but gives the x/y location relative to the main window
struct BobPorker : public Bob
{
	void (*action)(int,int);

	BobPorker(int x_, int y_, int w_, int h_, void (*action_)(int,int));
	virtual void click(int mx, int my);
};

// focus is the current captureing Bob if the mouse is down
extern Bob* focus;

} // namespace gui

// end of file
