#!/bin/python

import curses

def main(stdscr):
    curses.start_color()
    curses.use_default_colors()

    WIDTH = 32

    for i in range(0, curses.COLORS):
        curses.init_pair(i + 1, i, -1)
    for i in range(0, 255):
        if (i % WIDTH == 0):
            stdscr.addstr(i // WIDTH, 0, f"{i} - {i + WIDTH - 1}", 0)

        stdscr.addstr(i // WIDTH, i % WIDTH + 12, "x", curses.color_pair(i))

    stdscr.getch()
    stdscr.clear()

    for i in range(0, curses.COLORS):
        curses.init_pair(i + 1, -1, i)
    for i in range(0, 255):
        if (i % WIDTH == 0):
            stdscr.addstr(i // WIDTH, 0, f"{i} - {i + WIDTH - 1}", 0)

        stdscr.addstr(i // WIDTH, i % WIDTH + 12, "x", curses.color_pair(i))

    stdscr.getch()
    stdscr.clear()

    for i in range(0, 16):
        curses.init_pair(i + 1, i, -1)
    for i in range(0, 16):
        curses.init_pair(i + 16 + 1, -1, i)

    for i in range(0, 16):
        stdscr.addstr(
            f"The color number {i} of the ",
            curses.color_pair(i)
        )
        stdscr.addstr(
            f"standard terminal pallete.\n",
            curses.color_pair(i + 16)
        )

    stdscr.getch()

curses.wrapper(main)
