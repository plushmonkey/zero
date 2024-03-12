# zero
SubSpace/Continuum network bot. 

## Security
Connecting to SSC zones requires cryptinuum security solver service unless you have a VIE bypass. This is private to keep the integrity of the game.

Without the security solver, you can connect to asss zones that are using `enc_null` module and `Security::SecurityKickoff` to set 0 or by using `CAP_BYPASS_SECURITY`.

The bot can be changed to VIE encryption in `game/Settings.h` by changing the encrypt_method to `EncryptMethod encrypt_method = EncryptMethod::Subspace;`.
This will allow you to connect to `enc_vie` zones.

## Building
### Getting source
1. `git clone https://github.com/plushmonkey/zero`
2. `git submodule init && git submodule update`

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

### Config
1. Open `main.cpp`
2. Set `kServerIndex` to index into one of the servers listed in `kServers` list.
3. Set `kLoginName` and `kLoginPassword` to the login details.

### Debug renderer
1. Open `ZeroBot.cpp`
2. Change `CREATE_RENDER_WINDOW` to 1
3. Debugging the behavior tree execution path can be done in `BotController.cpp` by setting `should_print` to true.

