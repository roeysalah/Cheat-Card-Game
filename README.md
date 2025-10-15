# 🁏 Cheat — Multiplayer Card Game

A **multiplayer card game** where the goal is simple: **get rid of all your cards without getting caught!**

This project was implemented in **C** and demonstrates advanced **network programming concepts**, including **TCP client-server communication**, **multithreading**, and **multicasting**.

---

## 🚀 Features

- **Multiple simultaneous games**  
  Up to 3 games can run in parallel. When a game ends, a new one automatically starts on the same port.

- **Lobby and matchmaking system**  
  Each game starts with a lobby waiting for at least two players.  
  New connections are informed when a game is already running and must wait for the next round.

- **Turn-based gameplay with timers**  
  The server manages turn order and sets a timer for each turn.  
  If a player doesn’t act in time, the turn automatically passes to the next player.

- **Real-time updates via multicast**  
  All moves and events are announced to every player through multicast messaging.

- **Error handling & reconnection logic**  
  Invalid moves (like playing out of turn) are detected and reported immediately.  
  If a player disconnects, the game continues as long as at least two players remain.  
  The last connected player wins automatically.

---

## ⚙️ Implementation Details

- Each game runs in its **own process**.  
  When a game ends, the process restarts itself via `execv()`.

- The **listening socket** is managed by a **dedicated thread**, which tracks new connections and delegates them to game threads.

- The **server’s game thread**:
  - Uses `select()` to monitor all client sockets.
  - Manages player turns, decks, and the discard pile.
  - Handles timeouts and state transitions between turns.

- The **client**:
  - Has a **multicast handler thread** that receives and displays real-time updates.
  - Accepts user commands via CLI (`declare`, `cheat`, `take`, `quit`, etc.).
  - Provides immediate feedback for every action.

---

## 💬 Communication Protocol

| Message | Direction | Description |
|----------|------------|-------------|
| `H(len)(name)` | Client → Server | Hello message (connect) |
| `W(#player)` | Server → Client | Welcome with player number |
| `M(len)(addr)(#players)` | Server → Client | Sends multicast address |
| `T(#turn)` | Server → Client | Indicates next player’s turn |
| `c` | Client → Server | Declare cheat |
| `t` | Client → Server | Take a card |
| `P:(H/C/S/D)(value)` | Server → All | Card played to the table |
| `F(#winner)` | Server → All | Declare winner |
| `L(cards…)$` | Server → Client | Give cards to cheat loser |
| `e` | Server → Client | Invalid move |
| `Q` | Client → Server | Quit game |

---

## 🧠 Finite State Machines

### 🖥️ Server FSM
- **WAITING_FOR_PLAYERS** → **GAME_RUNNING** → **GAME_OVER** → **RESET**

### 👤 Client FSM
- **CONNECTING** → **WAITING** → **PLAYING** → **GAME_OVER**

---

## 🧉️ Technologies Used

- **C language**
- **POSIX sockets (TCP & multicast)**
- **Multithreading**
- **Process management (`fork`, `execv`)**
- **CLI interface**

---

## 🎮 How to Run

### 🧞‍♂️ Prerequisites
- Linux or macOS environment
- GCC compiler

### 🛠️ Build
```bash
gcc -pthread server.c -o server
gcc -pthread client.c -o client
```

### ▶️ Run
1. Start the server:
   ```bash
   ./server <port_number>
   ```
2. Start each client:
   ```bash
   ./client <server_ip> <port_number> <player_name>
   ```

3. Follow on-screen instructions to declare cards, cheat, or take from the deck.

---


