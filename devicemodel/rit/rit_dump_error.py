#!/usr/bin/python

import json
import os
from os import listdir
from functools import reduce
import sys
import configparser
import struct
import binascii
import operator
import getopt

class ErrorDumper:
	path = os.getcwd()
	name = None
	cfg_ext = '.cfg'
	fail_ext = '.out'
	cfg_name = os.path.join(path, 'rit' + cfg_ext)
	dump_name = None

	def analyze_sections(self):
		print ('parsing '+self.cfg_name+'\r')
		config = configparser.RawConfigParser(allow_no_value=True)
		config.read(self.cfg_name)
		i = 0
		sec_name = 'ErrorInfo_MemDump'
		sections = []
		while (config.has_option(sec_name, 'StartAddress' + str(i))):
			opt_addr = config.get(sec_name, 'StartAddress' + str(i))[2:]
			opt_len = config.get(sec_name, 'Range' + str(i))
			opt_name = config.get(sec_name, 'Name' + str(i))
			section = {'addr': opt_addr, 'len': opt_len, 'name': opt_name}
			sections.append(section)
			i += 1
		return sections

	def format_dumps(self, sections):
		# Binary to Hex
		comptext = ''
		i = 0
		for out_num in range(5):
			self.dump_name = os.path.join(self.path, 'rit' + '.' + str(out_num) + self.fail_ext)
			exists = os.path.isfile(self.dump_name)
			if exists:
				print (self.dump_name+' already exists\r')
			else:
				ofile = open(self.dump_name, 'w')
				print ('out file='+self.dump_name+'\r')
				break

		for sec in sections:
			dumpfile = os.path.join(self.path, 'rit_err_'+str(i)+'.dump')
			sec_name = sec['name']+'::'+sec['addr']+'::'+sec['len']
			read = 0
			ifile = open(dumpfile, 'rb')
			for read in range(int(sec['len'])):
				byte = ifile.read(1)
				read += 1
				hexstr = binascii.hexlify(byte).decode('ascii')
				comptext += hexstr+'  '
			ifile.close()
			text = reduce(operator.concat, comptext)
			ofile.write('['+sec_name+']\n'+comptext+'\n\n')
			comptext = ''
			i += 1
		ofile.close()

def main(argv=None):
	path = "."
	print ("start......\r")
	errDumper = ErrorDumper()
	try:
		sections = errDumper.analyze_sections()
		errDumper.format_dumps(sections);
	except:
		print ("Unexpected error")

if __name__ == "__main__":
    sys.exit(main())