import os,socket,subprocess,tempfile,unittest
from pathlib import Path
BINARY=os.environ.get('KV_BINARY','./cedar-kv')
class Server:
 def __init__(self,path):
  with socket.socket() as s:s.bind(('127.0.0.1',0));self.port=s.getsockname()[1]
  self.p=subprocess.Popen([BINARY,str(self.port),str(path)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
  if not self.p.stdout.readline().startswith('READY'):raise RuntimeError(self.p.communicate())
 def connect(self):
  s=socket.create_connection(('127.0.0.1',self.port),timeout=5);return s,s.makefile('rwb',buffering=0)
 def stop(self):
  if self.p.poll() is None:self.p.kill()
  _,err=self.p.communicate()
  if 'Sanitizer' in err or 'runtime error:' in err:raise AssertionError(err)
def command(f,s):
 f.write((s+'\n').encode());return f.readline().decode().rstrip('\n')
class Tests(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.path=Path(self.tmp.name)/'data.log';self.server=Server(self.path)
 def tearDown(self):self.server.stop();self.tmp.cleanup()
 def test_operations_restart(self):
  s,f=self.server.connect()
  with s,f:
   for cmd,reply in [('GET missing','NOT_FOUND'),('SET x hello world','OK'),('SET x replacement','OK'),('GET x','VALUE replacement'),('SET empty ','OK'),('GET empty','VALUE '),('SET gone value','OK'),('DELETE gone','OK'),('STATS','KEYS 2')]:self.assertEqual(command(f,cmd),reply)
  self.server.stop();self.server=Server(self.path)
  s,f=self.server.connect()
  with s,f:self.assertEqual(command(f,'GET x'),'VALUE replacement');self.assertEqual(command(f,'GET gone'),'NOT_FOUND')
 def test_framing_and_invalid_input(self):
  s,f=self.server.connect()
  with s,f:
   f.write(b'SE');f.write(b'T a b\nGET a\nPING\n')
   self.assertEqual([f.readline() for _ in range(3)],[b'OK\n',b'VALUE b\n',b'PONG\n'])
   for bad in ['UNKNOWN','SET','DELETE','SET '+('k'*129)+' v','SET k '+('v'*5000)]:self.assertTrue(command(f,bad).startswith('ERR'))
   f.write(b'SET z bad\x00value\n');self.assertTrue(f.readline().startswith(b'ERR'))
   self.assertEqual(command(f,'PING'),'PONG')
 def test_torn_tail(self):
  s,f=self.server.connect()
  with s,f:self.assertEqual(command(f,'SET safe durable'),'OK')
  self.server.stop()
  with self.path.open('ab') as log:log.write(b'SET torn partial')
  self.server=Server(self.path);s,f=self.server.connect()
  with s,f:self.assertEqual(command(f,'GET safe'),'VALUE durable');self.assertEqual(command(f,'GET torn'),'NOT_FOUND');self.assertEqual(command(f,'SET next valid'),'OK')
  self.assertNotIn(b'torn',self.path.read_bytes())
 def test_complete_corruption(self):
  self.server.stop();self.path.write_bytes(b'BROKEN record\n')
  p=subprocess.run([BINARY,'19998',str(self.path)],capture_output=True,timeout=5)
  self.assertNotEqual(p.returncode,0);self.assertIn(b'Invalid complete journal',p.stderr)
  self.assertEqual(self.path.read_bytes(),b'BROKEN record\n')
 def test_exclusive_lock(self):
  p=subprocess.run([BINARY,'19997',str(self.path)],capture_output=True,timeout=5)
  self.assertNotEqual(p.returncode,0);self.assertIn(b'locked',p.stderr)
 def test_reference_map(self):
  s,f=self.server.connect();expected={}
  with s,f:
   for i in range(1000):
    k=str(i%431);v='value'+str(i);self.assertEqual(command(f,f'SET {k} {v}'),'OK');expected[k]=v
   for k,v in expected.items():self.assertEqual(command(f,'GET '+k),'VALUE '+v)
   self.assertEqual(command(f,'STATS'),'KEYS '+str(len(expected)))
if __name__=='__main__':unittest.main(verbosity=2)
