import IPython.ipapi
import pyspec.spec as spec

def magic_loadspec(self, args):
	api = self.api
	api.ex("SPECFILE = spec.SpecDataFile(%s)" % args)

def magic_getspec(self,args):
	api = self.api
	api.ex("SPECSCAN = SPECFILE[%s]" % args)
	api.ex("SPECPLOT = SPECSCAN.plot()")
	
	# Now load variables
	
	specscan = ip.user_ns['SPECSCAN']
	for i in specscan.scandata.values.keys():
		foo = specscan.scandata.values[i]
		eval("ip.user_ns.update(dict(%s=foo))" % i)
		
def magic_prints(self,args):
	api = self.api
	api.ex("SPECPLOT.prints()")

def magic_fitto(self,args):
	api = self.api
	
	func = args.split()
	
	f = "[%s" % func[0]
	
	for i in func[1:]:
		f = f + ", %s" % i 
		
	f = f + "]"
	
	print "---- Fitting to %s" % f
	api.ex("SPECPLOT.fit(s)" % f)
	
def magic_reload(self, args):
	api = self.api
	api.ex("SPECFILE.index()")
	
def magic_header(self, args):
	self.api.ex("print SPECSCAN.header")
	
ip = IPython.ipapi.get()
ip.expose_magic('getspec', magic_getspec)	
ip.expose_magic('loadspec', magic_loadspec)
ip.expose_magic('prints', magic_prints)
ip.expose_magic('fitto', magic_fitto)
ip.expose_magic('updatespec', magic_reload)
ip.expose_magic('header', magic_header)