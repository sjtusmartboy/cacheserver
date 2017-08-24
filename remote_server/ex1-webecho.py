from twisted.protocols import basic
from twisted.internet import protocol, reactor
import os


class HTTPEchoProtocol(basic.LineReceiver):
    def __init__(self):
        self.lines = []
        self.file = []
        self.filename=[]

    def lineReceived(self, line):
    	print(111)
    	print(line)
        self.lines.append(line)
        if not line:
        	self.readFile()
        	self.sendResponse()
            
    def readFile(self):
    	first_request_line = self.lines[0].split(' ')
    	self.filename = '.' + first_request_line[1]
    	self.file = open(self.filename, 'r').read()
    	print(self.file)

    def sendResponse(self):
        self.sendLine("HTTP/1.1 200 OK")
        #self.sendLine("\r\n")
        
        filesize = os.path.getsize(self.filename)
        print(filesize)
        self.sendLine("content-length" + ": " + str(filesize))
        self.sendLine("\r\n")
        #responseBody = "You said:\r\n\r\n" + "\r\n".join(self.lines)
        responseBody = self.file
        #print responseBody
        self.transport.write(responseBody)
        self.transport.loseConnection()

class HTTPEchoFactory(protocol.ServerFactory):
    def buildProtocol(self, addr):
        return HTTPEchoProtocol()

reactor.listenTCP(8888, HTTPEchoFactory())
reactor.run()

#ps:join usage:
#>>seq1 = ['hello','good','boy','doiido']
#>>print ':'.join(seq1)
#result is 'hello:good:boy:doiido'

