#include "xmltv.h"
#include <stdlib.h>
#include <string>
#include <cstdlib>
#include <libxml/tree.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>


inline static bool startsWith(const char* s1, const char* s2) {
    while (*s2 != '\0') {
        if (*s1 != *s2) {
            return false;
        }

        s1++;
        s2++;
    }

    return true;
}

inline std::string readXmlFile (const std::string& path) {
    std::ostringstream buf;
    std::ifstream input (path.c_str());
    buf << input.rdbuf();
    return buf.str();
}

time_t getEpoch(const char* xmltime) {
    struct tm buf;
    buf.tm_isdst = -1;

    strptime(xmltime, "%0Y%0m%0d%H%M%S %z", &buf);
    time_t epoch = timegm(&buf);

    return epoch;
}

xsltStylesheetPtr Xmltv::pxsltStylesheet = NULL;

Xmltv::Xmltv() {
    stmtByFileRef = NULL;
    selectByTag = NULL;
    selectDistBySource = NULL;
    selectId = NULL;
    stmtMarkOldEvents = NULL;

}

Xmltv::~Xmltv() {
    delete stmtByFileRef;
    delete selectByTag;
    delete selectDistBySource;
    delete selectId;
    delete stmtMarkOldEvents;

    if (pxsltStylesheet)
        xsltFreeStylesheet(pxsltStylesheet);
}

int Xmltv::init(cEpgd *aObject, int aUtf8) {
    Plugin::init(aObject, aUtf8);

    if (pxsltStylesheet == NULL) {
        pxsltStylesheet = loadXSLT(getSource(), confDir, utf8);
    }

    return done;
}

int Xmltv::initDb() {
    int status = success;

    // --------
    // by fileref (for pictures)
    // select name from fileref
    //     where source = ? and fileref = ?

    stmtByFileRef = new cDbStatement(obj->fileDb);

    stmtByFileRef->build("select ");
    stmtByFileRef->bind("Name", cDBS::bndOut);
    stmtByFileRef->build(" from %s where ", obj->fileDb->TableName());
    stmtByFileRef->bind("Source", cDBS::bndIn | cDBS::bndSet);
    stmtByFileRef->bind("FileRef", cDBS::bndIn | cDBS::bndSet, " and ");

    status += stmtByFileRef->prepare();

    /*
     * by filename
     * select tag from fileref
     * where name = ?;
     */
    selectByTag = new cDbStatement(obj->fileDb);

    selectByTag->build("select ");
    selectByTag->bind("Tag", cDBS::bndOut);
    selectByTag->build(" from %s where ", obj->fileDb->TableName());
    selectByTag->bind("name", cDBS::bndIn | cDBS::bndSet);

    status += selectByTag->prepare();

    // --------
    // select distinct extid from channelmap
    //   where source = ?

    selectDistBySource = new cDbStatement(obj->mapDb);
    selectDistBySource->build("select ");
    selectDistBySource->bind("ExternalId", cDBS::bndOut, "distinct ");
    selectDistBySource->build(" from %s where ", obj->mapDb->TableName());
    selectDistBySource->bind("Source", cDBS::bndIn | cDBS::bndSet);

    status += selectDistBySource->prepare();


    // ---------
    // select channelid, mergesp from channelmap
    //     where source = ? and extid = ?

    selectId = new cDbStatement(obj->mapDb);
    selectId->build("select ");
    selectId->bind("ChannelId", cDBS::bndOut);
    selectId->bind("MergeSp", cDBS::bndOut, ", ");
    selectId->bind("Merge", cDBS::bndOut, ", ");
    selectId->build(" from %s where ", obj->mapDb->TableName());
    selectId->bind("Source", cDBS::bndIn | cDBS::bndSet);
    selectId->bind("ExternalId", cDBS::bndIn | cDBS::bndSet, " and ");

    status += selectId->prepare();

    // ----------
    // update events
    //   set updflg = case when updflg in (...) then 'D' else updflg end,
    //       delflg = 'Y',
    //       updsp = unix_timestamp()
    //   where source = '...'
    //     and (source, fileref) not in (select source,fileref from fileref)

    stmtMarkOldEvents = new cDbStatement(obj->eventsDb);

    stmtMarkOldEvents->build("update %s set ", obj->eventsDb->TableName());
    stmtMarkOldEvents->build("updflg = case when updflg in (%s) then 'D' else updflg end, ",
                             cEventState::getDeletable());
    stmtMarkOldEvents->build("delflg = 'Y', updsp = unix_timestamp()");
    stmtMarkOldEvents->build(" where source = '%s'", getSource());
    stmtMarkOldEvents->build(" and  (source, fileref) not in (select source,fileref from fileref)");

    status += stmtMarkOldEvents->prepare();

    return status;
}

int Xmltv::ready() {
    static int count = na;

    if (count == na) {
        char *where;

        asprintf(&where, "source = '%s'", getSource());

        if (obj->mapDb->countWhere(where, count) != success)
            count = na;

        free(where);
    }

    return count > 0;
}

int Xmltv::exitDb() {
    delete stmtByFileRef;
    stmtByFileRef = NULL;

    delete selectByTag;
    selectByTag = NULL;

    delete selectDistBySource;
    selectDistBySource = NULL;

    delete selectId;
    selectId = NULL;

    delete stmtMarkOldEvents;
    stmtMarkOldEvents = NULL;

    return success;
}

int Xmltv::atConfigItem(const char *Name, const char *Value) {
    if (!strcasecmp(Name, "input")) inputFile = std::string(Value);
    else if (!strcasecmp(Name, "getdata")) getDataScript = std::string(Value);
    else return fail;

    return success;
}

int Xmltv::processDay(int day, int fullupdate, Statistic *stat) {

    if (day != 0) {
        tell(1, "Skipping day %d for XMLTV plugin, since all days ar performed on day 0", day);
        return success;
    }

    // check if script is configured and call it to get epg data
    if (!getDataScript.empty()) {
        int ret = std::system(getDataScript.c_str());
        if (ret != 0) {
            tell(0, "Call of script xmltv.getdata (%s) failed. Aborting...", getDataScript.c_str());
            return fail;
        }
    }

    std::string date = getRelativeDate(day);

    obj->connection->startTransaction();

    // loop over all extid's of channelmap

    obj->mapDb->clear();
    obj->mapDb->setValue("Source", getSource());

    // load xml data only once
    std::string xmlData;
    if (startsWith(inputFile.c_str(), "http") || startsWith(inputFile.c_str(), "https")) {
        // http url
        int fileSize = 0;
        MemoryStruct data;
        int status = obj->downloadFile(inputFile.c_str(), fileSize, &data);

        if (status != success || isEmpty(data.name)) {
            tell(0, "Download header for day (%d) at '%s' failed, aborting, got name '%s', status was %d", day, inputFile.c_str(), data.name, status);
            status = fail;
            return status;
        } else {
            tell(0, "Download successful for day (%d) at '%s' success, got name '%s'", day, inputFile.c_str(), data.name);
        }

        xmlData = std::string(data.memory, fileSize);
    } else {
        // local file
        xmlData = readXmlFile(inputFile);
    }

    for (int res = selectDistBySource->find(); res && !obj->doShutDown(); res = selectDistBySource->fetch()) {
        std::string extid = obj->mapDb->getStrValue("ExternalId");
        std::string filename = extid + "-" + date;

        obj->fileDb->clear();
        obj->fileDb->setValue("name", filename.c_str());
        bool inFileRef = selectByTag->find();

        std::string eTag = obj->fileDb->getStrValue("Tag");

        selectByTag->freeResult();

        // Collect image URI's
        collectImageUris(xmlData);

        std::string fileRef = extid + "-" + date + "-" + eTag;

        if (processXml(xmlData, extid, fileRef.c_str()) != success) {
            stat->rejected++;
        }
        else {
            obj->fileDb->clear();
            obj->fileDb->setValue("Name", filename.c_str());
            obj->fileDb->setValue("Source", getSource());
            obj->fileDb->setValue("ExternalId", extid.c_str());
            obj->fileDb->setValue("FileRef", fileRef.c_str());
            obj->fileDb->setValue("Tag", eTag.c_str());
            if (inFileRef)
                obj->fileDb->update();
            else
                obj->fileDb->store();

            obj->connection->commit();
            usleep(100000);
            obj->connection->startTransaction();

            obj->fileDb->reset();

            stat->bytes += xmlData.size();
            stat->files++;
        }
    }

    obj->connection->commit();

    if (!obj->doShutDown())
       downloadImages();

    return success;
}

int Xmltv::processXml(const std::string &xmlDoc, const std::string &extid, const std::string &fileRef) {
    xmlDocPtr transformedDoc;
    xmlNodePtr xmlRoot;
    int count = 0;

    xmlDocPtr doc = xmlReadMemory(xmlDoc.c_str(), xmlDoc.length(), "noname.xml", NULL, 0);

    // Transform the generated XML
    transformedDoc = xsltApplyStylesheet(pxsltStylesheet, doc, 0);
    xmlFreeDoc(doc);
    if (transformedDoc == NULL) {
        // huh? some error...
        return fail;
    }

    /*
     * Get event-nodes from xml, parse and insert node by node
     */

    if (!(xmlRoot = xmlDocGetRootElement(transformedDoc))) {
        tell(1, "Invalid xml document returned from xslt for '%s', ignoring", fileRef.c_str());
        return fail;
    }

    obj->mapDb->clear();
    obj->mapDb->setValue("ExternalId", extid.c_str());
    obj->mapDb->setValue("Source", getSource());

    for (int f = selectId->find(); f && obj->dbConnected(); f = selectId->fetch()) {
        const char *channelId = obj->mapDb->getStrValue("ChannelId");

        for (xmlNodePtr node = xmlRoot->xmlChildrenNode; node && obj->dbConnected(); node = node->next) {
            int insert;
            char *prop = 0;
            tEventId eventid;

            // skip all unexpected elements

            if (node->type != XML_ELEMENT_NODE || strcmp((char *) node->name, "event") != 0)
                continue;

            // load only desired channels.

            if (!(prop = (char *) xmlGetProp(node, (xmlChar *) "channel")) || !*prop || !(extid == std::string(prop))) {
                continue;
            }

            xmlFree(prop);

            // get/check id

            if (!(prop = (char *) xmlGetProp(node, (xmlChar *) "eventid")) || !*prop || !(eventid = atoll(prop))) {
                xmlFree(prop);
                tell(0, "Missing event id, ignoring!");
                continue;
            }

            // get start and stop
            std::string start;
            std::string stop;
            std::string duration;


            char* startProp = (char*) xmlGetProp(node, (xmlChar *) "start");
            char *stopProp = (char*) xmlGetProp(node, (xmlChar *) "stop");

            for (xmlNodePtr childs = node->xmlChildrenNode; childs && obj->dbConnected(); childs = childs->next) {
                if (childs->type != XML_ELEMENT_NODE || ((strcmp((char *) childs->name, "starttime") != 0) && (strcmp((char *) childs->name, "duration") != 0)))
                    continue;

                time_t startEpoch = getEpoch(startProp);
                time_t stopEpoch = getEpoch(stopProp);

                // TODO: Warum müssen hier starttime und duration vertauscht werden?
                //       Was verstehe ich nicht?
                if (strcmp((char *) childs->name, "starttime") != 0) {
                    xmlNodeSetContent(childs, (const xmlChar*)std::to_string(stopEpoch - startEpoch).c_str());
                }

                if (strcmp((char *) childs->name, "duration") != 0) {
                    xmlNodeSetContent(childs, (const xmlChar*)std::to_string(startEpoch).c_str());
                }
            }

            xmlFree(startProp);
            xmlFree(stopProp);

            // create event ..

            obj->eventsDb->clear();
            obj->eventsDb->setBigintValue("EventId", eventid);
            obj->eventsDb->setValue("ChannelId", channelId);

            insert = !obj->eventsDb->find();

            obj->eventsDb->setValue("Source", getSource());
            obj->eventsDb->setValue("FileRef", fileRef.c_str());

            // auto parse and set other fields

            obj->parseEvent(obj->eventsDb->getRow(), node);

            // ...

            time_t mergesp = obj->mapDb->getIntValue("MergeSp");
            long starttime = obj->eventsDb->getIntValue("StartTime");
            long merge = obj->mapDb->getIntValue("Merge");

            // store ..

            if (insert) {
                // handle insert

                obj->eventsDb->setValue("Version", 0xFF);
                obj->eventsDb->setValue("TableId", 0L);
                obj->eventsDb->setValue("UseId", 0L);

                if (starttime <= mergesp)
                    obj->eventsDb->setCharValue("UpdFlg", cEventState::usInactive);
                else
                    obj->eventsDb->setCharValue("UpdFlg",
                                                merge > 1 ? cEventState::usMergeSpare : cEventState::usActive);

                obj->eventsDb->insert();
            }
            else {
                if (obj->eventsDb->hasValue("DelFlg", "Y"))
                    obj->eventsDb->setValue("DelFlg", "N");

                if (obj->eventsDb->hasValue("UpdFlg", "D")) {
                    if (starttime <= mergesp)
                        obj->eventsDb->setCharValue("UpdFlg", cEventState::usInactive);
                    else
                        obj->eventsDb->setCharValue("UpdFlg",
                                                    merge > 1 ? cEventState::usMergeSpare : cEventState::usActive);
                }

                obj->eventsDb->update();
            }

            obj->eventsDb->reset();
            count++;
        }
    }

    xmlSaveFileEnc("/tmp/file.xml", transformedDoc, "UTF-8");

    selectId->freeResult();

    xmlFreeDoc(transformedDoc);

    tell(2, "XML File '%s' processed, updated %d events", fileRef.c_str(), count);

    return success;

}

int Xmltv::cleanupAfter() {
    stmtMarkOldEvents->execute();
    return success;
}

// TODO: FIXME
int Xmltv::collectImageUris(const std::string &xmlDoc) {
    // get all Images via Xpath "//image/sizeX [X = 1,2,3,4]"

    if (!EpgdConfig.getepgimages)
        return success;

    /* Init libxml */
    xmlInitParser();
    stringstream xpathExprSS;
    xpathExprSS << "//image[position() <= " << EpgdConfig.maximagesperevent << "]/size" << imageSize;
    std::string xpathExpr = xpathExprSS.str();
    xmlDocPtr doc = xmlReadMemory(xmlDoc.c_str(), xmlDoc.length(), "noname.xml", NULL, 0);

    /* Create xpath evaluation context */
    xmlXPathContextPtr xpathCtx = xmlXPathNewContext(doc);
    if (xpathCtx == NULL) {
        tell(1, "Error: unable to create new XPath context");
        xmlFreeDoc(doc);
        return fail;
    }

    /* Evaluate xpath expression */
    xmlXPathObjectPtr xpathObj = xmlXPathEvalExpression(BAD_CAST xpathExpr.c_str(), xpathCtx);
    xmlChar *image = NULL;

    if (xpathObj) {
        xmlNodeSetPtr nodeset = xpathObj->nodesetval;
        if (nodeset) {
            for (int i = 0; i < nodeset->nodeNr; i++) {
                image = xmlNodeListGetString(doc, nodeset->nodeTab[i]->xmlChildrenNode, 1);
                // push the URI to the Queue
                imagefileSet.insert(std::string((char *) image));
                xmlFree(image);
            }
        }
    }

    /* Cleanup of XPath data */
    xmlXPathFreeObject(xpathObj);
    xmlXPathFreeContext(xpathCtx);

    /* free the document */
    xmlFreeDoc(doc);

    /* Shutdown libxml */
    xmlCleanupParser();
    return success;
}

void Xmltv::downloadImages() {
    MemoryStruct data;
    int fileSize = 0;
    std::stringstream path;

    path << EpgdConfig.cachePath << "/" << getSource() << "/";

    tell(0, "Downloading images...");
    int n = 0;
    for (std::set<std::string>::iterator it = imagefileSet.begin(); it != imagefileSet.end() && !obj->doShutDown(); ++it) {
        // check if file is not on disk
        std::size_t found = it->find_last_of("/");
        if (found == std::string::npos) continue;
        std::string filename = it->substr(found + 1);
        std::string fullpath = path.str() + filename;

        if (!fileExists(fullpath.c_str())) {

            data.clear();
            if (obj->downloadFile(it->c_str(), fileSize, &data, 30, userAgent()) != success) {
                tell(1, "Download at '%s' failed", it->c_str());
                continue;
            }
            obj->storeToFs(&data, filename.c_str(), getSource());
            tell(4, "Downloaded '%s' to '%s'", it->c_str(), fullpath.c_str());
            n++;
        }
    }
    imagefileSet.clear();
    tell(0, "Downloaded %d images", n);
}

int Xmltv::getPicture(const char *imagename, const char *fileRef, MemoryStruct *data) {
    data->clear();
    obj->loadFromFs(data, imagename, getSource());
    return data->size;
}

std::string Xmltv::getRelativeDate(int offsetDays) {
    time_t t = time(0);   // get time now
    t += (offsetDays * 60 * 60 * 24);
    struct tm *now = localtime(&t);
    std::stringstream date;
    date << (now->tm_year + 1900) << '-'
    << std::setfill('0') << std::setw(2) << (now->tm_mon + 1)
    << '-' << std::setfill('0') << std::setw(2) << now->tm_mday;
    return date.str();
}

extern "C" void *EPGPluginCreator() { return new Xmltv(); }











