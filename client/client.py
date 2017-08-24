import asyncio
async def tcp_echo_client(uri, loop):
	filepth = '.' + uri
	print('filepth: %r' % filepth)
	header = 'GET ' +  filepth + ' HTTP/1.1' + '\r\n' +  'Host: ' + 'cacheserver' + '\r\n' + '\r\n'
	reader, writer = await asyncio.open_connection('cacheserver', 3333, loop=loop)
	print('Send: %r' % uri)
	writer.write(header.encode())
	data = await reader.read(6666)
	print(data.decode('utf-8', 'ignore'))
	print("#################################################################################################")
	data = await reader.read(6666)
	print(data.decode('utf-8', 'ignore'))
	print("#################################################################################################")
	data = await reader.read(6666)
	print(data.decode('utf-8', 'ignore'))
	print("#################################################################################################")
	data = await reader.read(6666)
	print(data.decode('utf-8', 'ignore'))
	print("#################################################################################################")
	data = await reader.read(6666)
	print(data.decode('utf-8', 'ignore'))


	"""
	Dict = {}
	data = await reader.readline()
	print(data)
	while 1:
		data = await reader.readline()
		#data = await reader.readline()
		print(data)
		if(data == b"\r\n"):
			break
		data = data.split(b": ")
		
		Dict[data[0]] = int(data[1].decode().rstrip('\r\n'))
		#print(Dict[data[0]])
	
	data = await reader.readline()
	data = await reader.readline()
	print(data.decode('utf-8', 'ignore'))


	
	#print(int(Dict[b"content-length"]))
	data = await reader.read(int(Dict[b'content-length']))
	"""
	
	with open(filepth, 'w') as f:
		#f.write(data.decode())
		f.write(data.decode('utf-8', 'ignore'))
		
	
	#print('Received: %r' % data.decode())
	print('Close the socket')
	writer.close()
	
	
uri = '/hello'
loop = asyncio.get_event_loop()
loop.run_until_complete(tcp_echo_client(uri, loop))
loop.close()
