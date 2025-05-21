#pragma once

#include <string>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <ctime>

#include <vector>
#include <set>
#include <jansson.h>
#include <sstream>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include "epgd.h"
#include "lib/curl.h"

#define XMLTV_VERSION  "0.0.1"

class Xmltv : public Plugin {
private:
    static xsltStylesheetPtr pxsltStylesheet;

    cDbStatement* stmtMarkOldEvents;
    cDbStatement* selectDistBySource;
    cDbStatement* selectId;

    cDbStatement* stmtByFileRef;
    cDbStatement* selectByTag;

    std::set<std::string> imagefileSet;

    int imageSize;
    std::string inputFile;
    std::string getDataScript;

    std::string createActorsString(json_t* jdata);

    void createXmlNode(json_t* jdata, const char *jkey, xmlNodePtr parent);

    int downloadJson(const std::string chanId, const std::string day, std::string &etag, std::string &jsonDoc);

    void downloadImages();

    int collectImageUris(const std::string &xmlDoc);

    int jsonToXml(const std::string &jsonDoc, std::string &xmlDoc);

    int processXml(const std::string &xmlDoc, const std::string &extid, const std::string &fileRef);

    std::string getRelativeDate(int offsetDays);

    void SaveFile(const std::string &xmlDoc, std::string filename);

    const char* userAgent() { return "User-Agent: 4.2 (Nexus 10; Android 6.0.1; de_DE)"; }

public:

    Xmltv();

    ~Xmltv();

    virtual int init(cEpgd* aObject, int aUtf8);

    virtual int initDb();

    virtual int exitDb();

    virtual int atConfigItem(const char *Name, const char *Value);

    virtual const char *getSource() { return "xmltv"; }

    virtual int getPicture(const char *imagename, const char *fileRef, MemoryStruct *data);

    virtual int processDay(int day, int fullupdate, Statistic *stat);

    virtual int cleanupAfter();

    virtual int ready();
};
