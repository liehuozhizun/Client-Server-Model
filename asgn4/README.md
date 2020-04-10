Hang Yuan
hyuan3@ucsc.edu

Instructions:
	> make
	> ./httpserver -N <N_Thread Number, default:4> -c <N_blocks, default:40> -f <kvs filename> -m <alias filename> localhost:<port number>
	> ./httpclient localhost:<port number> <action requested>
	
Known bugs:
	It takes a quite long time to set up the empty entries for KVS file
	
Other information:
	When start server, please WAIT until you see the "READY TO USE" message.
  In this period, the server is initializing the kvs file.