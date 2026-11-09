import socket,sys
if len(sys.argv)<3:raise SystemExit('Usage: python3 client.py PORT "COMMAND" ...')
with socket.create_connection(('127.0.0.1',int(sys.argv[1])),timeout=5) as s:
 with s.makefile('rwb',buffering=0) as f:
  for cmd in sys.argv[2:]:
   f.write((cmd+'\n').encode('ascii'));print(f.readline().decode('ascii').rstrip('\n'))
