/** \mainpage Documentation of the LinAl 4
 * Created By David Coss, 2007
 */

#ifdef __DEBUG__
#define DEBUG_PRINT(x) std::cout << x << std::endl;
#else
#define DEBUG_PRINT(x)
#endif

#include <map>
#include <string>
#include <iostream>
#include <strstream>

//this prevents the GUI from being used.
#define GUI_CPP 1
#include "libdnstd/Double.h"
#include "libdnstd/StringTokenizer.h"
#include "Parser.h"

#include "Build.h"

using namespace std;
ios::fmtflags radix;
map<arg_t,int> equs;
vector<string> precompiledCode;

#include "opcodes.cpp"
Help makeHelp(){return assemblerHelp();}//for tab complete
const map<arg_t,opcode_t> lookupTable = assemblerTable();

string formatHex(const int& unformatted,ios::fmtflags currRadix)
{
  ostringstream os;
  os.precision(2);
  os.setf(currRadix,ios::basefield);
  os << unformatted;
  string returnMe = os.str();
  while(returnMe.size() < 2)
    returnMe = "0" + returnMe;
  return returnMe;
}

string formatHex(const string& unformatted)
{
  string semiformatted = unformatted;
  istringstream is;
  ios::fmtflags currRadix = radix;

  //check if format of the string overrides the default radix.
  if(semiformatted.substr(0,2) == "0x" || semiformatted.substr(0,2) == "0X")
    {
      currRadix = ios::hex;//because we've already formatted it into hex. So the "decimals" are literally in hex.
      semiformatted.erase(0,2);
    }
  else
    {
      switch(semiformatted.at(0))
	{
	case '.': case 'd': case 'D':
	  {
	    currRadix = ios::dec;
	    semiformatted.erase(0,1);
	    break;
	  }
	case 'o': case 'O':
	  {
	    currRadix = ios::oct;
	    semiformatted.erase(0,1);
	    break;
	  }
	default:
	  break;
	}
    }
  is.clear();is.str(semiformatted);is.setf(currRadix,ios::basefield);
  int val = -1;
  is >> val;
  
  if(is.fail())
    throw DavidException("Could not compile: " + unformatted);
  
  return formatHex(val,ios::hex);
}

std::string checkAndInsert(const Command& command)
{
	const args_t& tokens = command.getWords();
	const string& op = tokens.at(0);
	ostringstream os;
	os << "Incorrect number of arguments for "  << command.getWholeCommandString() << ": " << tokens.size()-1;
	string errorMessage = os.str();
	switch(tokens.size() - 1)//number of arguments
	{
	case 1:
	{
		if(op == "sett" || op == "setd" || op == "seta")
			return errorMessage;
		break;
	}
	case 2:
	{
		if(op == "sett" || op == "setd" || op == "seta")
			break;
		return errorMessage;
	}
	}
	for(args_t::const_iterator it = tokens.begin();it != tokens.end();it++)
		precompiledCode.push_back(*it);
	return "";
}

void compile(const args_t& precompiled, const string& filename)
{
	fstream file(filename.c_str(),std::ios::out);
	if(!file.is_open())
	{
		std::cerr << "Could not open " << filename << " for writting" << std::endl;
		return;
	}

	file << ":020000040000FA" << std::endl;//beginning line for 32-bit extension. Google Intel Hex for details.
	int checksum = 0;
	int counter = 0;
	int address = 0x4200;
	int hex;
	string currLine = "";
	for(args_t::const_iterator it = precompiled.begin();it != precompiled.end();it++)
	{
		if(counter % 8 == 0)
		{
			currLine += formatHex(address,std::ios_base::hex);
			checksum += (address & 0xff);
			checksum += ((address >> 8) & 0xff);
			address += 0x10;
		}
		if(lookupTable.find(*it) != lookupTable.end())
		{
			hex = lookupTable.at(*it);
			cout << *it << ": " <<  formatHex(hex,radix);
			currLine += formatHex(hex,radix);
			checksum += hex;
		}
		else if(equs.find(*it) != equs.end())
		{
			hex = equs.at(*it);
			cout << *it << ": " <<  formatHex(hex,radix);
			currLine += formatHex(hex,radix);
			checksum += hex;
		}
		else
		{
			cout << *it << ": " <<  formatHex(*it);
			currLine += formatHex(*it);
			string _val = *it;
			if(_val.substr(0,2) == "0x")
				_val.erase(0,2);
			istringstream is(_val);
			is.setf(radix,ios_base::basefield);
			is >> hex;
			if(is.fail())
			{
				throw DavidException("Invalid variable: " + *it);
			}
			checksum += hex;
		}
		currLine += "00";
		counter++;
		if(counter % 8 == 0)
		{
			cout << "hex: " << checksum << " eol: " <<  formatHex(checksum,radix);
			checksum &= 0xff;
			checksum ^= 0xff;
			checksum++;
			int numBytes = (currLine.size()-4)/4;
			file << ":" << ((numBytes < 10) ? "0" : "") << numBytes << currLine << formatHex(checksum,ios_base::hex) << endl;
			currLine = "";
			checksum = 0;
		}
	}
	if(counter % 8 != 0)
	{
		cout << "hex: " << checksum << " eol: " <<  formatHex(checksum,radix);
		checksum &= 0xff;
		checksum ^= 0xff;
		checksum++;
		currLine +=  (checksum < 0x10) ? "0" : "";
		int numBytes = (currLine.size()-4)/4;
		file << ":" << ((numBytes < 10) ? "0" : "") << numBytes << currLine << formatHex(checksum,ios_base::hex) << endl;
		currLine = "";
		checksum = 0;
	}
}

std::string print_function(const Command& command)
{
	if(command.getSecondWord() == "equ")
	{
		if(command.getWords().size() < 3)
			return "Usage: <variable> equ <literal>";
		istringstream is(command.getWords().at(2));
		int literalVal = -1;
		is >> literalVal;
		if(is.fail())
			return "equ requires an integer";
		equs[command.getCommandWord()] = literalVal;
		return command.getCommandWord() + " = " + command.getWords().at(2); 
	}
	else if(command.getCommandWord() == "compile")
	{
		if(command.getWords().size() != 2)
		{
			return "Usage: compile <filename>";
		}
		compile(precompiledCode,command.getSecondWord());
		return "Compiled. Saved as " + command.getSecondWord();
	}
	else if(command.getCommandWord() == "radix")
	  {
	    if(command.getWords().size() != 2)
	      {
		switch(radix)
		  {
		  case ios::hex:
		    return "hex";
		  case ios::dec:
		    return "dec";
		  case ios::oct:
		    return "oct";
		  }
	      }
	    const string& newRadix = command.getSecondWord();
	    if(newRadix == "dec")
	      {
		radix = ios::dec;
	      }
	    else if(newRadix == "hex")
	      {
		radix = ios::hex;
	      }
	    else if(newRadix == "oct")
	      {
		radix = ios::oct;
	      }
	    else
	      {
		return "Usage: radix [dec | hex | oct]";
	      }
	    return "Radix set to " + newRadix;	    
	  }
	else if(lookupTable.find(command.getCommandWord()) == lookupTable.end())
	  return "unknown command: " + command.getCommandWord();
	
    return checkAndInsert(command);
}

int main(int argc, char **argv)
{
  radix = ios::hex;
  //so that there aren't multiple main's
  #define __HAVE_MAIN__ 1

	if((argc > 1) && (DString(argv[1]) == "--todo"))
	{
		cout << Build::todo << endl;
		return 1;
	}
	if((argc > 1) && (DString(argv[1]) == "--version"))
	{
	  cout << "assembler version: " << Build::getVersion() << endl;
	  cout << "opcode version: " << opcodeVersion[0] << "." << opcodeVersion[1] << "." << opcodeVersion[2] << endl;
		cout << "Last Build: " << Build::getBuild() << endl;
		return 1;
	}
	if(argc > 1)
	{
		try{
		  Parser p;
                  p.setHelp(makeHelp());
		  p.main(argc,argv);
		  return 0;
		}
		catch(DavidException de)
		{
			de.stdOut();
			return de.getCode();
		}
	}
	else{
		try{
			Parser p;
			p.setHelp(assemblerHelp());
			p.main();
			return 0;
		}
		catch(DavidException de)
		{
			de.stdOut();
			return de.getCode();
		}
	}

}

