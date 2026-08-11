Cyber Surge ⚡

Cyber Surge is a fast-paced, addictive infinite terminal runner game written in pure C++17, designed specifically for Linux/Debian environments. Dodge deadly firewalls, collect quantum energy, and survive as the grid accelerates!

Features

Zero Dependencies: Pure C++ standard library with direct Linux terminal control (termios).

Single-File Architecture: Entirely self-contained in a single source file (game.cpp).

Agile Movement: Responsive 2-cell step movement for precise tactical dodging.

Dynamic Difficulty: Gradual level scaling with increasing speeds and obstacles.

Persistent High Scores: Automatically saves your best score in your home directory (~/.cyber_surge_score).

Clean ANSI Graphics: Polished ASCII/Unicode layout with terminal color support and death flash effects.

Controls

Key

Action

A or ←

Move Left (2 cells)

D or →

Move Right (2 cells)

W or ↑

Move Up (2 cells)

S or ↓

Move Down (2 cells)

P

Pause / Resume Game

Q

Return to Main Menu / Exit

Enter / R

Restart after Game Over

Compilation & Installation

Make sure you have a C++17 compatible compiler installed (like g++).

Clone or download the repository:

git clone https://github.com/shuxratbek20/cyber-surge.git
cd cyber-surge


Compile the game:

g++ -std=c++17 -O2 -o game game.cpp


Run the game:

./game


License

This project is open-source and distributed under the MIT License.
