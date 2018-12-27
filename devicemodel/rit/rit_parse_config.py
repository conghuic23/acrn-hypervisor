#!/usr/bin/python
import os
import sys
import configparser
import struct

class CafeConfigParser:
	path = os.getcwd()
	name = None
	cfg_ext = '.cfg'
	cfg_name = os.path.join(path, 'rit' + cfg_ext)
	print ("create empty byte array")

	def analyze_sections(self):
		print (self.cfg_name)
		config = configparser.RawConfigParser(allow_no_value=True)
		config.read(self.cfg_name)
		i = 0
		sec_name = 'ErrorInfo_MemDump'
		sections = []
		cfg_bin = open("rit_cfg.bin", "wb")
		while (config.has_option(sec_name, 'StartAddress' + str(i))):
			print ("inside loop")
			opt_addr = config.get(sec_name, 'StartAddress' + str(i))[2:]
			opt_len = config.get(sec_name, 'Range' + str(i))
			opt_name = config.get(sec_name, 'Name' + str(i))
			section = {'addr': opt_addr, 'len': int(opt_len), 'name': opt_name}
			sections.append(section)
			cfg_bin.write(struct.pack('i', int(opt_addr, 16)))
			cfg_bin.write(struct.pack('i', int(opt_len)))
			i += 1
		cfg_bin.close()
		return sections

def main(argv=None):
	path = "."
	print ("start......")
	cfgParser = CafeConfigParser()
	try:
		sections = cfgParser.analyze_sections()
		for sec in sections:
			print (sec['name'], sec['addr'], sec['len'])
	except:
		print ("Unexpected error")

if __name__ == "__main__":
    sys.exit(main())