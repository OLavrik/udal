#include <iostream>
#include <fstream>
#include <csignal>
#include <cstdlib>
#include <sys/stat.h>
#include <ctime>
#include <fcntl.h>
#include <list>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <unistd.h>
#include <syslog.h>

#define PID_LOGS "/var/run/daemon_lab_14.pid"
#define EPS 2
#define SLEEP_TIME 1

typedef enum {w = 30, d = 86400, h = 3600, NONE = 0} EventType; // 604800

void update_pid_log()
{
	std::ifstream iFile(PID_LOGS);
	if (iFile.is_open() && !iFile.eof())
	{
		pid_t p;
		iFile >> p;
		if (p > 0)
			kill(p, SIGTERM);
        }
	iFile.close();
	std::ofstream oFile(PID_LOGS);
	if (!oFile) {
		syslog(LOG_ERR, "Could not open pid logs");
		exit(EXIT_FAILURE);
	}
	oFile << getpid();
	oFile.close();
}

class Event {
public:
	Event(tm time, std::string text, EventType t)
	{
		_time = time;
		_text = text;
		_repeatTime = t;
		_done = false;
		_last_remind = 0;
	}
	bool checkTime(time_t &currTime);
	std::string getText() { return _text; }

private:
	tm _time;
	time_t _last_remind;
	bool _done;
	std::string _text;
	EventType _repeatTime;
};



bool Event::checkTime(time_t &currTime)
{
	double diff = std::difftime(std::mktime(&_time), currTime);
	if (!_done)
	{
		if (std::abs(diff) < 2 * EPS)
		{
			_done = true;
			return true;
		}
		return false;
	}

	if (_last_remind == 0)
	{
		_last_remind = std::mktime(&_time);
	}
	
	if (_repeatTime != NONE)
	{
		if (_last_remind + _repeatTime < currTime)
		{
			_last_remind += _repeatTime * ((currTime - _last_remind) / _repeatTime);
			return true;
		}
	}

	return false;
}


class Reminder {
public:
	void addEvent(Event ev) { events.push_back(ev); }
	void clear() { events.clear(); }
	void exec();
	void printText(std::string text);
	void setPath()
	{}
	Reminder() : _form("gnome-terminal -- ")
	{
		std::string _print_text_path = realpath("print_text", nullptr);
		_form += _print_text_path + " "; 
	}
	
private:
	std::string _form;
	std::list<Event> events;
	
} reminder;


void Reminder::exec()
{
	std::time_t t = std::time(0);   // get time now

	for (auto it = events.begin(); it != events.end(); it++)
	{
		if ((*it).checkTime(t))
			printText((*it).getText());
	}
}


void Reminder::printText(std::string text)
{
	std::string query = _form + text;
	system(query.c_str());
}



struct ConfigReader {
	void setPath(std::string path) { config_path = path; }
	void read();
	ConfigReader() {}

private:
	std::string config_path;

	bool canParseLine(std::string& line);
	bool isDateTime(std::string& str);
	Event parseEvent(std::string &line);
} config_reader;


bool ConfigReader::isDateTime(std::string& str)
{
	std::stringstream ss(str);
	tm time;
	ss >> std::get_time(&time, "%d.%m.%Y-%H:%M:%S");
	if (ss.fail())
		return false;
	return true;
}


bool ConfigReader::canParseLine(std::string & line)
{
	std::stringstream ss(line);
	std::string token;
	ss >> token;
	if (token != "add_event")
		return false;
	ss >> token;
	return isDateTime(token);
}


Event ConfigReader::parseEvent(std::string &line) {
	tm time;
	EventType t = NONE;
	std::string text;

	std::stringstream ss(line);
	std::string token;
	ss >> token;
	ss >> std::get_time(&time, "%d.%m.%Y-%H:%M:%S");
	ss >> token;
	if (token == "-w")
		t = w;
	else if (token == "-h")
		t = h;
	else if (token == "-d")
		t = d;
	else
		text += token;
	
	while (!ss.eof())
	{
		ss >> token;
		if (text.size() > 0)
			text += " ";
		text += token;
	}
	return Event(time, text, t);
}

void ConfigReader::read()
{
	std::ifstream config = std::ifstream(config_path);
	if (!config.is_open() || config.eof()) {
        	syslog(LOG_ERR, "Bad path or empty config");
        	exit(EXIT_FAILURE);
    	}

	std::string line;
	reminder.clear();
	while (std::getline(config, line))
	{
		if (canParseLine(line))
			reminder.addEvent(parseEvent(line));
	}
	
	config.close();
}


void signal_handler(int sgnl)
{
	switch(sgnl)
	{
	case SIGHUP:
		config_reader.read();
		syslog(LOG_NOTICE, "SIGHUP catched");
		break;
	case SIGTERM:
		syslog(LOG_NOTICE, "SIGTERM catched");
		unlink(PID_LOGS);
		exit(0);
		break;
	}
}


int main(int argc, char** argv)
{
	pid_t pid = fork();
	switch(pid)
	{
		case 0:
			break;
		case -1:
			exit(EXIT_FAILURE);
		default:
			syslog(LOG_NOTICE, "Soccessfully made fork. Child's pid is %d.", pid);
			exit(EXIT_SUCCESS);
	}


	if (argc < 2) {
		printf("Expected arguments.");
		exit(EXIT_FAILURE);
	}

	openlog("daemon_lab_14", LOG_NOWAIT | LOG_PID, LOG_USER);
	umask(0);

	if (setsid() < 0)
	{
		syslog(LOG_ERR, "Could not generate session process");
		exit(EXIT_FAILURE);
	}	
	
	pid = fork();
	switch(pid)
	{
		case 0:
			break;
		case -1:
			exit(EXIT_FAILURE);
		default:
			exit(EXIT_SUCCESS);
			syslog(LOG_NOTICE, "Soccessfully made fork. Child's pid is %d.", pid);
	}
	
	std::string config_path = realpath(argv[1], nullptr);
	config_reader.setPath(config_path);
	reminder.setPath();
	config_reader.read();

	if ((chdir("/")) < 0)
	{
		syslog(LOG_ERR, "Could not change directory to /");
		exit(EXIT_FAILURE);
    	}

	syslog(LOG_NOTICE, "Success config_path:%s", config_path.c_str());

	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

    	signal(SIGHUP, signal_handler);
	signal(SIGTERM, signal_handler);

	update_pid_log();

	while (true)
	{
		reminder.exec();
		sleep(SLEEP_TIME);
	}
	
/*
	std::string config_path = realpath(argv[1], nullptr);
	config_reader.setPath(config_path);
	config_reader.read();
	update_pid_log();

	while (true)
	{
		reminder.exec();
		sleep(EPS);
	}

*/
	return 0;
}
