#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <chrono>
#include <thread>
#include <fstream>
#include <algorithm>
#include <cstdlib>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <csignal>
#include <sstream>
#include <iomanip>

// Terminal raw mode guard for safe configuration restoration
class TermiosGuard {
private:
    struct termios oldt;
    bool active;
public:
    TermiosGuard() : active(false) {
        if (tcgetattr(STDIN_FILENO, &oldt) == 0) {
            struct termios newt = oldt;
            newt.c_lflag &= ~(ICANON | ECHO);
            newt.c_cc[VMIN] = 0;
            newt.c_cc[VTIME] = 0;
            if (tcsetattr(STDIN_FILENO, TCSANOW, &newt) == 0) {
                int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
                fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
                std::cout << "\033[?25l"; // Hide cursor
                std::cout << "\033[2J\033[H"; // Clear screen
                active = true;
            }
        }
    }

    ~TermiosGuard() {
        if (active) {
            tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
            int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
            fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);
            std::cout << "\033[?25h"; // Show cursor
            std::cout << "\033[0m\n"; // Reset colors
        }
    }
};

enum class GameState {
    MENU,
    PLAYING,
    PAUSED,
    GAMEOVER,
    HOWTO,
    HIGHSCORE,
    EXIT
};

enum class EntityType {
    OBSTACLE_RED, // Lethal red obstacle (1-hit KO)
    ITEM_ENERGY   // Score booster
};

enum class CellType {
    EMPTY,
    BORDER,
    PLAYER,
    OBSTACLE_RED,
    ITEM_ENERGY
};

struct GameObject {
    int x, y;
    EntityType type;
    int width;
    int height;
};

class Game;
static Game* globalGameInstance = nullptr;

extern "C" void handleSignal(int sig) {
    if (globalGameInstance) {
        std::cout << "\033[?25h\033[0m\n";
    }
    std::exit(0);
}

class Game {
private:
    GameState state;
    int menuSelection;
    long long score;
    long long bestScore;
    int level;
    int speedTickRate; // milliseconds per frame
    unsigned long long frameCounter;
    int deathFlashTimer;

    // Field dimensions
    static const int FIELD_WIDTH = 30;
    static const int FIELD_HEIGHT = 16;

    // Compact player configuration: 2 columns wide, 1 row high (2x1)
    int playerX;
    int playerY;
    static const int PLAYER_WIDTH = 2;
    static const int PLAYER_HEIGHT = 1;

    std::vector<GameObject> entities;
    std::mt19937 rng;

    std::string getHighScorePath() const {
        const char* home = std::getenv("HOME");
        if (home) {
            return std::string(home) + "/.cyber_surge_score";
        }
        return ".cyber_surge_score";
    }

    void loadHighScore() {
        std::ifstream file(getHighScorePath());
        if (file.is_open()) {
            file >> bestScore;
            file.close();
        } else {
            bestScore = 0;
        }
    }

    void saveHighScore() {
        if (score > bestScore) {
            bestScore = score;
            std::ofstream file(getHighScorePath());
            if (file.is_open()) {
                file << bestScore;
                file.close();
            }
        }
    }

public:
    Game() : state(GameState::MENU), menuSelection(0), score(0), bestScore(0),
             level(1), speedTickRate(35), frameCounter(0), deathFlashTimer(0),
             playerX(FIELD_WIDTH / 2 - 1), playerY(FIELD_HEIGHT - 2) {
        std::random_device rd;
        rng.seed(rd());
        loadHighScore();
        globalGameInstance = this;
        std::signal(SIGINT, handleSignal);
    }

    void run() {
        TermiosGuard termGuard;
        while (state != GameState::EXIT) {
            auto frameStart = std::chrono::steady_clock::now();

            processInput();
            update();
            render();

            auto frameEnd = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(frameEnd - frameStart).count();
            
            // Ultra-responsive input polling during sleep
            long long remainingSleep = speedTickRate - elapsed;
            while (remainingSleep > 0 && state == GameState::PLAYING) {
                long long chunk = std::min(4LL, remainingSleep);
                std::this_thread::sleep_for(std::chrono::milliseconds(chunk));
                processInput();
                remainingSleep -= chunk;
            }
            if (remainingSleep > 0 && state != GameState::PLAYING) {
                std::this_thread::sleep_for(std::chrono::milliseconds(remainingSleep));
            }
        }
    }

private:
    void processInput() {
        char buf[32];
        ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
        if (n <= 0) return;

        for (ssize_t i = 0; i < n; ++i) {
            char c = buf[i];

            // Handle arrow keys sequences (ESC [ A/B/C/D)
            if (c == 27 && i + 2 < n && buf[i+1] == '[') {
                char arrow = buf[i+2];
                i += 2;
                if (state == GameState::PLAYING) {
                    if (arrow == 'A') playerY = std::max(1, playerY - 2);
                    else if (arrow == 'B') playerY = std::min(FIELD_HEIGHT - 1, playerY + 2);
                    else if (arrow == 'C') playerX = std::min(FIELD_WIDTH - 1 - PLAYER_WIDTH, playerX + 2);
                    else if (arrow == 'D') playerX = std::max(1, playerX - 2);
                } else if (state == GameState::MENU) {
                    if (arrow == 'A') {
                        menuSelection = (menuSelection - 1 + 4) % 4;
                    } else if (arrow == 'B') {
                        menuSelection = (menuSelection + 1) % 4;
                    }
                }
                continue;
            }

            if (state == GameState::MENU) {
                if (c == 'w' || c == 'W') {
                    menuSelection = (menuSelection - 1 + 4) % 4;
                } else if (c == 's' || c == 'S') {
                    menuSelection = (menuSelection + 1 + 4) % 4;
                } else if (c == '\n' || c == '\r') {
                    executeMenuSelection();
                } else if (c == 'q' || c == 'Q') {
                    state = GameState::EXIT;
                }
            } else if (state == GameState::PLAYING) {
                if (c == 'a' || c == 'A') {
                    playerX = std::max(1, playerX - 2);
                } else if (c == 'd' || c == 'D') {
                    playerX = std::min(FIELD_WIDTH - 1 - PLAYER_WIDTH, playerX + 2);
                } else if (c == 'w' || c == 'W') {
                    playerY = std::max(1, playerY - 2);
                } else if (c == 's' || c == 'S') {
                    playerY = std::min(FIELD_HEIGHT - 1, playerY + 2);
                } else if (c == 'p' || c == 'P') {
                    state = GameState::PAUSED;
                } else if (c == 'q' || c == 'Q') {
                    state = GameState::MENU;
                }
            } else if (state == GameState::PAUSED) {
                if (c == 'p' || c == 'P' || c == '\n' || c == '\r') {
                    state = GameState::PLAYING;
                } else if (c == 'q' || c == 'Q') {
                    state = GameState::MENU;
                }
            } else if (state == GameState::GAMEOVER) {
                if (c == '\n' || c == '\r' || c == 'r' || c == 'R') {
                    startNewGame();
                } else if (c == 'q' || c == 'Q') {
                    state = GameState::MENU;
                }
            } else if (state == GameState::HOWTO || state == GameState::HIGHSCORE) {
                if (c == 'q' || c == 'Q' || c == '\n' || c == '\r') {
                    state = GameState::MENU;
                }
            }
        }
    }

    void executeMenuSelection() {
        switch (menuSelection) {
            case 0:
                startNewGame();
                break;
            case 1:
                state = GameState::HOWTO;
                break;
            case 2:
                state = GameState::HIGHSCORE;
                break;
            case 3:
                state = GameState::EXIT;
                break;
        }
    }

    void startNewGame() {
        score = 0;
        level = 1;
        speedTickRate = 35;
        frameCounter = 0;
        deathFlashTimer = 0;
        playerX = FIELD_WIDTH / 2 - 1;
        playerY = FIELD_HEIGHT - 2;
        entities.clear();
        state = GameState::PLAYING;
    }

    void update() {
        if (state != GameState::PLAYING) return;

        frameCounter++;
        score += level * 2;

        if (deathFlashTimer > 0) {
            deathFlashTimer--;
        }

        // Gentle level scaling & gradual speed increase
        level = 1 + score / 400;
        speedTickRate = std::max(20, 35 - (level - 1) * 1);

        // Spawn fewer obstacles and more energy items for balanced gameplay
        std::uniform_int_distribution<int> distCol(1, FIELD_WIDTH - 4);
        std::uniform_int_distribution<int> chanceDist(1, 100);

        int spawnChance = std::min(50, 25 + level * 2);
        if (chanceDist(rng) <= spawnChance) {
            int c = chanceDist(rng);
            GameObject obj;
            obj.x = distCol(rng);
            obj.y = 0;
            if (c <= 65) {
                // Red obstacle: 1-hit KO, width 2
                obj.type = EntityType::OBSTACLE_RED;
                obj.width = 2;
                obj.height = 1;
            } else {
                // Energy item for bonus points
                obj.type = EntityType::ITEM_ENERGY;
                obj.width = 1;
                obj.height = 1;
            }
            entities.push_back(obj);
        }

        // Move entities down
        for (auto& e : entities) {
            e.y++;
        }

        // Check collisions and bounds
        auto it = entities.begin();
        while (it != entities.end()) {
            bool hitPlayer = (it->x < playerX + PLAYER_WIDTH && it->x + it->width > playerX &&
                              it->y >= playerY && it->y < playerY + PLAYER_HEIGHT);
            
            if (hitPlayer) {
                if (it->type == EntityType::OBSTACLE_RED) {
                    deathFlashTimer = 10;
                    saveHighScore();
                    state = GameState::GAMEOVER;
                    return;
                } else if (it->type == EntityType::ITEM_ENERGY) {
                    score += 200;
                }
                it = entities.erase(it);
            } else if (it->y >= FIELD_HEIGHT - 1) {
                it = entities.erase(it);
            } else {
                ++it;
            }
        }
    }

    void render() const {
        std::ostringstream ss;
        ss << "\033[H"; // Move cursor to top-left

        if (state == GameState::MENU) {
            ss << "\n";
            ss << "  ╔══════════════════════════════╗\n";
            ss << "  ║    CYBER SURGE: EASY MODE    ║\n";
            ss << "  ╠══════════════════════════════╣\n";
            ss << "  ║                              ║\n";
            ss << "  ║   " << (menuSelection == 0 ? "\033[31m> 1. NEW GAME\033[0m" : "  1. NEW GAME") << "             ║\n";
            ss << "  ║   " << (menuSelection == 1 ? "\033[36m> 2. HOW TO PLAY\033[0m" : "  2. HOW TO PLAY") << "         ║\n";
            ss << "  ║   " << (menuSelection == 2 ? "\033[36m> 3. BEST SCORE\033[0m" : "  3. BEST SCORE") << "          ║\n";
            ss << "  ║   " << (menuSelection == 3 ? "\033[36m> 4. EXIT\033[0m" : "  4. EXIT") << "                ║\n";
            ss << "  ║                              ║\n";
            ss << "  ╚══════════════════════════════╝\n";
            ss << "   Use W/S / Arrows & Enter to select.\n";
        } 
        else if (state == GameState::HOWTO) {
            ss << "\n";
            ss << "  ╔══════════════════════════════╗\n";
            ss << "  ║         HOW TO PLAY          ║\n";
            ss << "  ╠══════════════════════════════╣\n";
            ss << "  ║  ██   : Player (Steps 2 cells)║\n";
            ss << "  ║  XX   : Red Obstacle (1-HIT) ║\n";
            ss << "  ║  ◆    : Energy (+200 Pts)    ║\n";
            ss << "  ║                              ║\n";
            ss << "  ║  Rule: AVOID RED OBSTACLES!  ║\n";
            ss << "  ║                              ║\n";
            ss << "  ║  A/D or ←/→ : Move Left/Rt   ║\n";
            ss << "  ║  W/S or ↑/↓ : Move Up/Dn     ║\n";
            ss << "  ║  P : Pause  |  Q : Menu      ║\n";
            ss << "  ╚══════════════════════════════╝\n";
            ss << "    Press Q or Enter to return.\n";
        }
        else if (state == GameState::HIGHSCORE) {
            ss << "\n";
            ss << "  ╔══════════════════════════════╗\n";
            ss << "  ║         HALL OF FAME         ║\n";
            ss << "  ╠══════════════════════════════╣\n";
            ss << "  ║                              ║\n";
            ss << "  ║   BEST SCORE: \033[33m" << std::setw(10) << bestScore << "\033[0m  ║\n";
            ss << "  ║                              ║\n";
            ss << "  ╚══════════════════════════════╝\n";
            ss << "    Press Q or Enter to return.\n";
        }
        else if (state == GameState::PLAYING || state == GameState::PAUSED) {
            std::string borderCol = (deathFlashTimer > 0) ? "\033[31m" : "\033[36m";

            // Header stats
            ss << borderCol << " ╔════════════════════════════════╗\033[0m\n";
            ss << borderCol << " ║\033[0m SCR:" << std::setw(6) << score << "  BEST:" << std::setw(6) << bestScore << borderCol << "     ║\033[0m\n";
            ss << borderCol << " ║\033[0m MODE: \033[32mEASY RUN\033[0m          LVL:" << std::setw(2) << level << borderCol << "  ║\033[0m\n";
            ss << borderCol << " ╠════════════════════════════════╣\033[0m\n";

            // Build grid using CellType enum (avoids multi-character character constant warnings)
            std::vector<std::vector<CellType>> grid(FIELD_HEIGHT, std::vector<CellType>(FIELD_WIDTH, CellType::EMPTY));

            // Borders
            for (int y = 0; y < FIELD_HEIGHT; ++y) {
                grid[y][0] = CellType::BORDER;
                grid[y][FIELD_WIDTH - 1] = CellType::BORDER;
            }

            // Draw entities
            for (const auto& e : entities) {
                for (int wx = 0; wx < e.width; ++wx) {
                    int cx = e.x + wx;
                    if (cx > 0 && cx < FIELD_WIDTH - 1 && e.y > 0 && e.y < FIELD_HEIGHT) {
                        if (e.type == EntityType::OBSTACLE_RED) {
                            grid[e.y][cx] = CellType::OBSTACLE_RED;
                        } else {
                            grid[e.y][cx] = CellType::ITEM_ENERGY;
                        }
                    }
                }
            }

            // Draw compact 2x1 player
            for (int px = playerX; px < playerX + PLAYER_WIDTH; ++px) {
                if (px > 0 && px < FIELD_WIDTH - 1 && playerY > 0 && playerY < FIELD_HEIGHT) {
                    grid[playerY][px] = CellType::PLAYER;
                }
            }

            // Render grid with colors (using string literal for border to prevent -Wmultichar warning)
            for (int y = 0; y < FIELD_HEIGHT; ++y) {
                ss << borderCol << " ║\033[0m";
                for (int x = 0; x < FIELD_WIDTH; ++x) {
                    CellType cell = grid[y][x];
                    if (cell == CellType::BORDER) {
                        ss << "│";
                    } else if (cell == CellType::PLAYER) {
                        ss << "\033[1;36m██\033[0m";
                        x += 1; // skip next cell for 2-wide player block
                    } else if (cell == CellType::OBSTACLE_RED) {
                        ss << "\033[1;31mXX\033[0m";
                        x += 1; // skip next cell for 2-wide red obstacle block
                    } else if (cell == CellType::ITEM_ENERGY) {
                        ss << "\033[36m◆\033[0m";
                    } else {
                        ss << ' ';
                    }
                }
                ss << borderCol << "║\033[0m\n";
            }
            ss << borderCol << " ╚════════════════════════════════╝\033[0m\n";
            if (state == GameState::PAUSED) {
                ss << " \033[33m*** PAUSED (P to resume) ***\033[0m\n";
            } else {
                ss << " Controls: WASD/Arrows (2-Cell step)\n";
            }
        }
        else if (state == GameState::GAMEOVER) {
            ss << "\n";
            ss << "  ╔══════════════════════════════╗\n";
            ss << "  ║         FATAL HIT!           ║\n";
            ss << "  ╠══════════════════════════════╣\n";
            ss << "  ║                              ║\n";
            ss << "  ║   FINAL SCORE: " << std::setw(8) << score << "    ║\n";
            ss << "  ║   BEST SCORE:  " << std::setw(8) << bestScore << "    ║\n";
            ss << "  ║                              ║\n";
            ss << "  ║   [Enter / R] Play Again     ║\n";
            ss << "  ║   [Q] Return to Menu         ║\n";
            ss << "  ║                              ║\n";
            ss << "  ╚══════════════════════════════╝\n";
        }

        std::cout << ss.str() << std::flush;
    }
};

int main() {
    Game game;
    game.run();
    return 0;
}
