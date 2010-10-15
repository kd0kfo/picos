#ifndef HELP_CPP
#define HELP_CPP

#include <map>
#include <string>

#include "CommandWords.h"

class Help : public std::map<arg_t,std::string>{
public:
  Help(){}
    ~Help(){}

    const std::string& getHelp(const arg_t&);
    static Help getDefault();
    
};


#endif