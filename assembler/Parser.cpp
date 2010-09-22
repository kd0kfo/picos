#include "Parser.h"

Parser::Parser() {
#ifdef __USE_READLINE__
    rl_bind_key('\t', Parser::tab_complete);
#endif
    commands = new CommandWords();
    h = new Help();
    inputBuffer = new arg_t("");
    history = new args_t();
}

Parser::~Parser() {
    delete commands;
    delete h;
    delete history;
    h = 0;
    history = 0;
}

void Parser::main() {
    this->start();
}

void Parser::main(int argc, char **args) {
    arg_t arrargs();
    for (int i = 1; i < argc; i++) {
        arrargs.push_back(args[i]);
    }
    main(arrargs);
}

void Parser::main(args_t& args) {
    if (args.size() >= 1 && args.at(0) == "--version") {
        std::cout << "Version " + Build::getVersion() + " built on " + Build::getBuild() << std::endl;
        return;
    }
    if (args.size() >= 1 && args.at(0) == "--build") {
        return;
    }

    if (args.size() >= 1 && (args.at(0) == "--help" || args.at(0) == "--usage")) {
        std::cout << DString("LinAl ") <<  Build::getVersion() << std::endl;
        std::cout << DString("Last built on: ") << Build::getBuild() << std::endl;
        std::cout << "Symbolic Calculator"<< std::endl;
        std::cout << "Usage: linal [options]"<< std::endl;
        std::cout << "Options:"<< std::endl;
        std::cout << "--build, Shows the build number and versions of all classes"<< std::endl;
        std::cout << "--calc <expression>, Calculates the given expression"<< std::endl;
        std::cout << "--version, Shows the version"<< std::endl;
        std::cout << "--gui, Starts LinAl graphically"<< std::endl;
        std::cout << "--console, Start LinAll within a console."<< std::endl;
        std::cout << "--help, this"<< std::endl;
        return;
    } else if (args.size() >= 1 && args.at(0) == "--console") {
        start();
    } else {
        start();
    }

}

Command * Parser::getCommand() {
    using namespace std;
    char inputLine[256]; // will hold the full input line

    try {
#ifndef _READLINE_H_
        cout << "> "; // print prompt
        cin.get(inputLine, 256);
#else
        strcpy(inputLine, readline("> "));
        add_history(inputLine);
#endif
    } catch (...) {
        throw DavidException("Input error");
    }
    cout.flush();
    cin.ignore(255, '\n');

    if (strlen(inputLine) == 0 || std::string(inputLine) == "") {
        return new Command("help");
    } else {
        return new Command(inputLine);
    }
}

/**
 * Print out a list of valid command words.
 */
void Parser::printHelp(DString& whatIsSaid) {
    if (whatIsSaid.size() == 0)
        return;
    std::cout << DString("List of commands: ") << (whatIsSaid = commands->showAll()) << std::endl;
    std::cout << std::endl;
}

void Parser::start() {
    std::cout << DString("Welcome to linal, version:") << getVersion() << "\nFor help with the program, enter help" << std::endl;
    // Enter the main command loop.  Here we repeatedly read commands and
    // execute them until the game is over.

    bool finished = false;
    while (!finished) {
        try {
            Command * command = getCommand();
            finished = processCommand(command);
            delete command;
            command = 0;
        } catch (DavidException e) {
            std::cout << "Process Error: " << e.getMessage() << std::endl;
        }
    }
    std::cout << "Thank you.  Good bye." << std::endl;
}

bool Parser::processCommand(Command& command) {
    std::string dummyString = "";
    bool returnMe = processCommand(command, dummyString);
    delete dummyString;
    dummyString = 0;
    return returnMe;
}

bool Parser::processCommand(Command& command, std::string& whatIsSaid) {
    bool wantToQuit = false;

    if (!command.getCommandWord() == "save"  && command.getCommandWord() != "exit" && !command->getCommandWord().equals("quit") && command.getCommandWord() != "history")
        history.push_back(command.getWholeCommandString());


    if (command.isUnknown()) {
        Command newCommand("print " + command.getCommandWord());
        std::string printString = Command::print(newCommand);
        std::cout << printString << std::endl;
        whatIsSaid = printString;

        return false;
    }

    std::string commandWord = command.getCommandWord();
    if (commandWord== "help" && command.getSecondWord() == "") {
        printHelp(whatIsSaid);
    } else if (commandWord == "help" && command.getSecondWord() != "") {

        whatIsSaid = h->getHelp(command->getSecondWord());
        std::cout << whatIsSaid << std::endl;
    } else if (commandWord == "version") {
        std::cout << "Version: " << Build::getVersion() << std::endl;
    } else if (commandWord == "print") {
        whatIsSaid = Command::print(*command);
        std::cout << whatIsSaid << std::endl;
    }
    else if (commandWord == "save") {
        if (!command.hasSecondWord()) {
            std::cout << "file name needed with save." << std::endl;
        } else {
            std::string fileName = command.getSecondWord();
            std::cout << "add save routine." << std::endl;
            //std::cout << "File saved as " << fileName << std::endl;
        }
    } else if (commandWord == "history") {
        if (history.size() == 0) {
            std::cout << "No commands have been entered yet." << std::endl;
            return false;
        }
        std::cout << "Previous Commands:");
        int counter = 0;
        for (args_t::iterator it = history.begin();
                it != history.end(); it++)
                {
                std::cout << counter++ << ": " << *it << std::endl;
                }
    }else if (commandWord == "quit" || commandWord == "exit") {
        wantToQuit = true;
    }

    return wantToQuit; /**/
}

arg_t Parser::getHistoryValue(Command& command) const {

    size_t historyCount = history.size() - 1;
    if (command.getCommandWord().substr(0, 2) != "!!")
        historyCount = (int) Double(command->getCommandWord().substr(1)).doubleValue();

    if (historyCount < 0)
        throw DavidException("I need a positive number for the place in history");

    if (historyCount >= history->size())
        throw DavidException("Ummm, that is in the future.");

    DString newCommand = history->get(historyCount);
    utils::DArray<DString> words = command->getWords();
    for (int i = 1; i < words.size(); i++)
        newCommand += DString(" ") + words.get(i);
    return newCommand;

}

DString Parser::getSwapValue(Command * command) const {


    using utils::StringTokenizer;
    StringTokenizer tokie(command->getWholeCommandString(), "^");

    DString lhs, rhs;
    DavidException hopefullyNot("There need to be two ^ each with a string after them.");

    if (!tokie.hasMoreTokens())
        throw hopefullyNot;

    lhs = tokie.nextToken();

    if (!tokie.hasMoreTokens())
        throw hopefullyNot;

    rhs = tokie.nextToken();


    DString lastCommand;

    if (history->size() >= 1)
        lastCommand = history->get(history->size() - 1);
    else
        throw DavidException("There are no previous commands");



    StringTokenizer newTokie(lastCommand);

    DString newCommand;

    while (newTokie.hasMoreTokens()) {
        DString curr = newTokie.nextToken();
        if (curr == lhs)
            curr = rhs;

        newCommand += DString(" ") + curr;
    }


    return newCommand.trim();

}

DHashMap<DString> Parser::storedVariableArray(const LinAl * const la) {
    DHashMap<linal::LTree> variables = la->getCopyVariables();

    std::vector<DString> keys = variables.getKeys();
    std::vector<linal::LTree> objs = variables.getObjects();

    DHashMap<DString> returnMe;

    for (int i = 0; i < keys.size(); i++) {
        returnMe.put(keys[i], objs[i].toDString());
    }

    return returnMe;
}

int Parser::tab_complete(int a, int b) {
    Parser p;
    p.printHelp(0);
    return 0;
}

