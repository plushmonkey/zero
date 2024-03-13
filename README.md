# zero
SubSpace/Continuum network bot. 

## Security
Connecting to SSC zones requires cryptinuum security solver service unless you have a VIE bypass. This is private to keep the integrity of the game.

Without the security solver, you can connect to asss zones that are using `enc_null` module and `Security::SecurityKickoff` to set 0 or by using `CAP_BYPASS_SECURITY`.

Set `Encryption = Subspace` in the config file to use VIE encryption.

## Config
1. Copy `zero.cfg.dist` to `zero.cfg`
2. Open `zero.cfg` and modify the login details.

Config file order:
1. Command line first argument `./zero.exe mybot.cfg`
2. `zero.cfg` if nothing is specified on command line.
3. `zero.cfg.dist` if `zero.cfg` was not found.

## Building
### Getting source
1. `git clone https://github.com/plushmonkey/zero`
2. `cd zero`
3. `git submodule init && git submodule update`

### Windows
1. Open `zero.sln`
2. Switch to Release x64 build
3. Build

### Linux
1. Install GLFW3 development libraries (`sudo apt-get install libglfw3-dev` on Ubuntu)
2. Install cmake
3. Open terminal in zero directory
4. `cmake -B build -S .`
5. `cd build`
6. `make -j 12`
7. `cp ../zero.cfg.dist zero.cfg`

### Debug renderer
1. Open `ZeroBot.cpp`
2. Change `CREATE_RENDER_WINDOW` to 1
3. Debugging the behavior tree execution path can be done in `BotController.cpp` by setting `should_print` to true.

