#!python
import os
import shutil
import sys
import random

server = "127.0.0.1:5002"
dir = "generated"
count = 5

if len(sys.argv) > 1:
	count = int(sys.argv[1])

try:
	shutil.rmtree(dir)
except Exception as e:
	print(e)
	pass
	
try:
	os.mkdir(dir)
except:
	pass
	
for i in range(count):
	username = "ZeroBot" + str(i + 1)
	filename = dir + "/" + username + ".cfg"
	f = open(filename, "w")
	
	f.write("[Login]\n")
	f.write("Username = " + username + "\n")
	f.write("Password = local\n")
	f.write("Server = Subgame\n")
	f.write("Encryption = Subspace\n")
	f.write("\n")
	f.write("[General]\n")
	f.write("LogLevel = Error\n")
	
	ship = random.randint(1, 8)
	f.write("RequestShip = " + str(ship) + "\n")
	
	if random.randint(0, 1) == 1:
		f.write("Behavior = terrier\n")
	
	f.write("\n")
	f.write("[Servers]\n")
	f.write("Subgame = " + server + "\n")
	
	f.close()

f = open(dir + "/run.bat", "w")

for i in range(count):
	username = "ZeroBot" + str(i + 1)
	f.write("start /B ../zero.exe " + username + ".cfg\n")
	if i > 0 and i % 10 == 0:
		wait_time = int((i / 10) + 1)
		f.write("%WINDIR%\\system32\\timeout.exe /t " + str(wait_time) + " /nobreak\n")
	
f.close()
