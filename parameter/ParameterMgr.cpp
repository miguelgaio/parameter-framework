/* <auto_header>
 * <FILENAME>
 * 
 * INTEL CONFIDENTIAL
 * Copyright © 2011 Intel 
 * Corporation All Rights Reserved.
 * 
 * The source code contained or described herein and all documents related to
 * the source code ("Material") are owned by Intel Corporation or its suppliers
 * or licensors. Title to the Material remains with Intel Corporation or its
 * suppliers and licensors. The Material contains trade secrets and proprietary
 * and confidential information of Intel or its suppliers and licensors. The
 * Material is protected by worldwide copyright and trade secret laws and
 * treaty provisions. No part of the Material may be used, copied, reproduced,
 * modified, published, uploaded, posted, transmitted, distributed, or
 * disclosed in any way without Intel’s prior express written permission.
 * 
 * No license under any patent, copyright, trade secret or other intellectual
 * property right is granted to or conferred upon you by disclosure or delivery
 * of the Materials, either expressly, by implication, inducement, estoppel or
 * otherwise. Any license under such intellectual property rights must be
 * express and approved by Intel in writing.
 * 
 *  AUTHOR: Patrick Benavoli (patrickx.benavoli@intel.com)
 * CREATED: 2011-06-01
 * UPDATED: 2011-07-27
 * 
 * 
 * </auto_header>
 */
#include "ParameterMgr.h"
#include "XmlParser.h"
#include "XmlParameterSerializingContext.h"
#include "XmlElementSerializingContext.h"
#include "SystemClass.h"
#include "ElementLibrarySet.h"
#include "SubsystemLibrary.h"
#include "NamedElementBuilderTemplate.h"
#include "KindElementBuilderTemplate.h"
#include "ElementBuilderTemplate.h"
#include "SelectionCriterionType.h"
#include "SubsystemElementBuilder.h"
#include "SelectionCriteria.h"
#include "ComponentType.h"
#include "ComponentInstance.h"
#include "ParameterBlockType.h"
#include "BooleanParameterType.h"
#include "IntegerParameterType.h"
#include "FixedPointParameterType.h"
#include "ParameterBlackboard.h"
#include "Parameter.h"
#include "ParameterBlackboard.h"
#include "ParameterAccessContext.h"
#include "XmlFileIncluderElement.h"
#include "ParameterFrameworkConfiguration.h"
#include "FrameworkConfigurationGroup.h"
#include "FrameworkConfigurationLocation.h"
#include "ConfigurableDomains.h"
#include "ConfigurableDomain.h"
#include "DomainConfiguration.h"
#include "XmlComposer.h"
#include "XmlDomainSerializingContext.h"
#include "BitParameterBlockType.h"
#include "BitParameterType.h"
#include "StringParameterType.h"
#include "EnumParameterType.h"
#include "RemoteProcessorServerInterface.h"
#include "ElementLocator.h"
#include "AutoLog.h"
#include "CompoundRule.h"
#include "SelectionCriterionRule.h"
#include "SimulatedBackSynchronizer.h"
#include "HardwareBackSynchronizer.h"
#include "AutoLock.h"
#include <strings.h>
#include <dlfcn.h>
#include <assert.h>
#include "ParameterHandle.h"
#include "LinearParameterAdaptation.h"
#include "EnumValuePair.h"

#define base CElement

// Used for remote processor server creation
typedef IRemoteProcessorServerInterface* (*CreateRemoteProcessorServer)(uint16_t uiPort, IRemoteCommandHandler* pCommandHandler);

// Global configuration file name (fixed)
const char* gacParameterFrameworkConfigurationFileName = "ParameterFrameworkConfiguration.xml";
const char* gacSystemSchemasSubFolder = "Schemas";

// Config File System looks normally like this:
// ---------------------------------------------
//├── <ParameterFrameworkConfiguration>.xml
//├── Schemas
//│   └── *.xsd
//├── Settings
//│   └── <SystemClassName folder>*
//│       ├── <ConfigurableDomains>.xml
//│       └── <Settings>.bin?
//└── Structure
//    └── <SystemClassName folder>*
//        ├── <SystemClassName>Class.xml
//        └── <Subsystem>.xml*
// --------------------------------------------


// Remote command parser array
const CParameterMgr::SRemoteCommandParserItem CParameterMgr::gastRemoteCommandParserItems[] = {
    /// Version
    { "version", &CParameterMgr::versionCommandProcess, 0, "", "Show version" },
    /// Status
    { "status", &CParameterMgr::statusCommandProcess, 0, "", "Show current status" },
    /// Tuning Mode
    { "setTuningMode", &CParameterMgr::setTuningModeCommmandProcess, 1, "on|off*", "Turn on or off Tuning Mode" },
    { "getTuningMode", &CParameterMgr::getTuningModeCommmandProcess, 0, "", "Show Tuning Mode" },
    /// Value Space
    { "setValueSpace", &CParameterMgr::setValueSpaceCommmandProcess, 1, "raw|real*", "Assigns Value Space used for parameter value interpretation" },
    { "getValueSpace", &CParameterMgr::getValueSpaceCommmandProcess, 0, "", "Show Value Space" },
    /// Output Raw Format
    { "setOutputRawFormat", &CParameterMgr::setOutputRawFormatCommmandProcess, 1, "dec*|hex", "Assigns format used to output parameter values when in raw Value Space" },
    { "getOutputRawFormat", &CParameterMgr::getOutputRawFormatCommmandProcess, 0, "", "Show Output Raw Format" },
    /// Sync
    { "setAutoSync", &CParameterMgr::setAutoSyncCommmandProcess, 1, "on*|off", "Turn on or off automatic synchronization to hardware while in Tuning Mode" },
    { "getAutoSync", &CParameterMgr::getAutoSyncCommmandProcess, 0, "", "Show Auto Sync state" },
    { "sync", &CParameterMgr::syncCommmandProcess, 0, "", "Synchronize current settings to hardware while in Tuning Mode and Auto Sync off" },
    /// Criteria
    { "listCriteria", &CParameterMgr::listCriteriaCommmandProcess, 0, "", "List selection criteria" },
    /// Domains
    { "listDomains", &CParameterMgr::listDomainsCommmandProcess, 0, "", "List configurable domains" },
    { "dumpDomains", &CParameterMgr::dumpDomainsCommmandProcess, 0, "", "Show all domains and configurations, including applicability conditions" },
    { "createDomain", &CParameterMgr::createDomainCommmandProcess, 1, "<domain>", "Create new configurable domain" },
    { "deleteDomain", &CParameterMgr::deleteDomainCommmandProcess, 1, "<domain>", "Delete configurable domain" },
    { "renameDomain", &CParameterMgr::renameDomainCommmandProcess, 2, "<domain> <new name>", "Rename configurable domain" },
    { "setSequenceAwareness", &CParameterMgr::setSequenceAwarenessCommmandProcess, 1, "<domain> true|false*", "Set configurable domain sequence awareness" },
    { "getSequenceAwareness", &CParameterMgr::getSequenceAwarenessCommmandProcess, 1, "<domain>", "Get configurable domain sequence awareness" },
    { "listDomainElements", &CParameterMgr::listDomainElementsCommmandProcess, 1, "<domain>", "List elements associated to configurable domain" },
    { "addElement", &CParameterMgr::addElementCommmandProcess, 2, "<domain> <elem path>", "Associate element at given path to configurable domain" },
    { "removeElement", &CParameterMgr::removeElementCommmandProcess, 2, "<domain> <elem path>", "Dissociate element at given path from configurable domain" },
    { "splitDomain", &CParameterMgr::splitDomainCommmandProcess, 2, "<domain> <elem path>", "Split configurable domain at given associated element path" },
    /// Configurations
    { "listConfigurations", &CParameterMgr::listConfigurationsCommmandProcess, 1, "<domain>", "List domain configurations" },
    { "createConfiguration", &CParameterMgr::createConfigurationCommmandProcess, 2, "<domain> <configuration>", "Create new domain configuration" },
    { "deleteConfiguration", &CParameterMgr::deleteConfigurationCommmandProcess, 2, "<domain> <configuration>", "Delete domain configuration" },
    { "renameConfiguration", &CParameterMgr::renameConfigurationCommmandProcess, 3, "<domain> <configuration> <new name>", "Rename domain configuration" },
    { "saveConfiguration", &CParameterMgr::saveConfigurationCommmandProcess, 2, "<domain> <configuration>", "Save current settings into configuration" },
    { "restoreConfiguration", &CParameterMgr::restoreConfigurationCommmandProcess, 2, "<domain> <configuration>", "Restore current settings from configuration" },
    { "setElementSequence", &CParameterMgr::setElementSequenceCommmandProcess, 3, "<domain> <configuration> <elem path list>", "Set element application order for configuration" },
    { "getElementSequence", &CParameterMgr::getElementSequenceCommmandProcess, 2, "<domain> <configuration>", "Get element application order for configuration" },
    { "setRule", &CParameterMgr::setRuleCommmandProcess, 3, "<domain> <configuration> <rule>", "Set configuration application rule" },
    { "clearRule", &CParameterMgr::clearRuleCommmandProcess, 2, "<domain> <configuration>", "Clear configuration application rule" },
    { "getRule", &CParameterMgr::getRuleCommmandProcess, 2, "<domain> <configuration>", "Get configuration application rule" },
    /// Elements/Parameters
    { "listElements", &CParameterMgr::listElementsCommmandProcess, 1, "<elem path>|/", "List elements under element at given path or root" },
    { "listParameters", &CParameterMgr::listParametersCommmandProcess, 1, "<elem path>|/", "List parameters under element at given path or root" },
    { "dumpElement", &CParameterMgr::dumpElementCommmandProcess, 1, "<elem path>", "Dump structure and content of element at given path" },
    { "getElementSize", &CParameterMgr::getElementSizeCommmandProcess, 1, "<elem path>", "Show size of element at given path" },
    { "showProperties", &CParameterMgr::showPropertiesCommmandProcess, 1, "<elem path>", "Show properties of element at given path" },
    { "getParameter", &CParameterMgr::getParameterCommmandProcess, 1, "<param path>", "Get value for parameter at given path" },
    { "setParameter", &CParameterMgr::setParameterCommmandProcess, 2, "<param path> <value>", "Set value for parameter at given path" },
    { "listBelongingDomains", &CParameterMgr::listBelongingDomainsCommmandProcess, 1, "<elem path>", "List domain(s) element at given path belongs to" },
    { "listAssociatedDomains", &CParameterMgr::listAssociatedDomainsCommmandProcess, 1, "<elem path>", "List domain(s) element at given path is associated to" },
    /// Browse
    { "listAssociatedElements", &CParameterMgr::listAssociatedElementsCommmandProcess, 0, "", "List element sub-trees associated to at least one configurable domain" },
    { "listConflictingElements", &CParameterMgr::listConflictingElementsCommmandProcess, 0, "", "List element sub-trees contained in more than one configurable domain" },
    { "listRogueElements", &CParameterMgr::listRogueElementsCommmandProcess, 0, "", "List element sub-trees owned by no configurable domain" },
    /// Settings Import/Export
    { "exportDomainsXML", &CParameterMgr::exportConfigurableDomainsToXMLCommmandProcess, 1, "<file path> ", "Export domains to XML file" },
    { "importDomainsXML", &CParameterMgr::importConfigurableDomainsFromXMLCommmandProcess, 1, "<file path>", "Import domains from XML file" },
    { "exportDomainsWithSettingsXML", &CParameterMgr::exportConfigurableDomainsWithSettingsToXMLCommmandProcess, 1, "<file path> ", "Export domains including settings to XML file" },
    { "importDomainsWithSettingsXML", &CParameterMgr::importConfigurableDomainsWithSettingsFromXMLCommmandProcess, 1, "<file path>", "Import domains including settings from XML file" },
    { "exportSettings", &CParameterMgr::exportSettingsCommmandProcess, 1, "<file path>", "Export settings to binary file" },
    { "importSettings", &CParameterMgr::importSettingsCommmandProcess, 1, "<file path>", "Import settings from binary file" }
};
// Remote command parsers array Size
const uint32_t CParameterMgr::guiNbRemoteCommandParserItems = sizeof(gastRemoteCommandParserItems) / sizeof(gastRemoteCommandParserItems[0]);

CParameterMgr::CParameterMgr(const string& strConfigurationFilePath) :
    _bTuningModeIsOn(false),
    _bValueSpaceIsRaw(false),
    _bOutputRawFormatIsHex(false),
    _bAutoSyncOn(true),
    _pMainParameterBlackboard(new CParameterBlackboard),
    _pElementLibrarySet(new CElementLibrarySet),
    _strXmlConfigurationFilePath(strConfigurationFilePath),
    _uiStructureChecksum(0),
    _pRemoteProcessorServer(NULL),
    _uiMaxCommandUsageLength(0),
    _pLogger(NULL),
    _uiLogDepth(0)
{
    // Tuning Mode Mutex
    bzero(&_blackboardMutex, sizeof(_blackboardMutex));
    pthread_mutex_init(&_blackboardMutex, NULL);

    // Deal with children
    addChild(new CParameterFrameworkConfiguration);
    addChild(new CSelectionCriteria);
    addChild(new CSystemClass);
    addChild(new CConfigurableDomains);

    // Feed element library
    feedElementLibraries();

    _pCommandHandler = new CCommandHandler(this);

    // Add command parsers
    uint32_t uiRemoteCommandParserItem;

    for (uiRemoteCommandParserItem = 0; uiRemoteCommandParserItem < guiNbRemoteCommandParserItems; uiRemoteCommandParserItem++) {

        const SRemoteCommandParserItem* pRemoteCommandParserItem = &gastRemoteCommandParserItems[uiRemoteCommandParserItem];

        _pCommandHandler->addCommandParser(pRemoteCommandParserItem->_pcCommandName,
                                           pRemoteCommandParserItem->_pfnParser,
                                           pRemoteCommandParserItem->_uiMinArgumentCount,
                                           pRemoteCommandParserItem->_pcHelp,
                                           pRemoteCommandParserItem->_pcDescription);
    }

    // Configuration file folder
    uint32_t uiSlashPos = _strXmlConfigurationFilePath.rfind('/', -1);

    assert(uiSlashPos != (uint32_t)-1);

    _strXmlConfigurationFolderPath = _strXmlConfigurationFilePath.substr(0, uiSlashPos);

    // Schema absolute folder location
    _strSchemaFolderLocation = _strXmlConfigurationFolderPath + "/" + gacSystemSchemasSubFolder;
}

CParameterMgr::~CParameterMgr()
{
    // Children
    delete _pRemoteProcessorServer;
    delete _pMainParameterBlackboard;
    delete _pElementLibrarySet;

    // Tuning Mode Mutex
    pthread_mutex_destroy(&_blackboardMutex);
}

string CParameterMgr::getKind() const
{
    return "ParameterMgr";
}

// Logging
void CParameterMgr::setLogger(CParameterMgr::ILogger* pLogger)
{
    _pLogger = pLogger;
}

// Logging
void CParameterMgr::doLog(const string& strLog) const
{
    if (_pLogger) {

        // Nest
        string strIndent;

        // Level
        uint32_t uiNbIndents = _uiLogDepth;

        while (uiNbIndents--) {

            strIndent += "    ";
        }

        // Log
        _pLogger->log(strIndent + strLog);
    }
}

void CParameterMgr::nestLog() const
{
    _uiLogDepth++;
}

void CParameterMgr::unnestLog() const
{
    _uiLogDepth--;
}

// Version
string CParameterMgr::getVersion() const
{
    string strVersion;

    // Major
    strVersion = toString(guiEditionMajor) + ".";
    // Minor
    strVersion += toString(guiEditionMinor) + ".";
    // Revision
    strVersion += toString(guiRevision);

    return strVersion;
}

bool CParameterMgr::load(string& strError)
{
    CAutoLog autoLog(this, "Loading");

    // Load Framework configuration
    if (!loadFrameworkConfiguration(strError)) {

        return false;
    }

    // Load subsystems
    if (!getSystemClass()->loadSubsystems(strError, _astrPluginFolderPaths)) {

        return false;
    }

    // Load structure
    if (!loadStructure(strError)) {

        return false;
    }

    // Load settings
    if (!loadSettings(strError)) {

        return false;
    }

    // Back synchronization for areas in parameter blackboard not covered by any domain
    CBackSynchronizer* pBackSynchronizer = createBackSynchronizer(strError);

    log("Main blackboard back synchronization");

    // Back-synchronize
    if (!pBackSynchronizer->sync()) {
        // Get rid of back synchronizer
        delete pBackSynchronizer;

        strError = "Main blackboard back synchronization failed: " + strError;

        return false;
    }
    // Get rif of back synchronizer
    delete pBackSynchronizer;

    // We're done loading the settings and back synchronizing
    CConfigurableDomains* pConfigurableDomains = getConfigurableDomains();

    // We need to ensure all domains are valid
    pConfigurableDomains->validate(_pMainParameterBlackboard);

    // Ensure application of currently selected configurations
    // Force-apply configurations
    if (!pConfigurableDomains->apply(_pMainParameterBlackboard, true, strError)) {

        return false;
    }

    // Start remote processor server if appropriate
    return handleRemoteProcessingInterface(strError);
}

bool CParameterMgr::loadFrameworkConfiguration(string& strError)
{
    CAutoLog autoLog(this, "Loading framework configuration");

    // Parse Structure XML file
    CXmlElementSerializingContext elementSerializingContext(strError);

    if (!xmlParse(elementSerializingContext, getFrameworkConfiguration(), _strXmlConfigurationFilePath, _strXmlConfigurationFolderPath, EFrameworkConfigurationLibrary)) {

        return false;
    }
    // Set class name to system class and configurable domains
    getSystemClass()->setName(getConstFrameworkConfiguration()->getSystemClassName());
    getConfigurableDomains()->setName(getConstFrameworkConfiguration()->getSystemClassName());

    // Get subsystem plugins folders element
    const CFrameworkConfigurationGroup* pSubsystemPluginFolders = static_cast<const CFrameworkConfigurationGroup*>(getConstFrameworkConfiguration()->findChild("SubsystemPluginFolders"));

    if (!pSubsystemPluginFolders) {

        strError = "Parameter Framework Configuration: couldn't find SubsystemPluginFolders element";

        return false;
    }
    // Get plugin locations
    uint32_t uiPluginFolderLocation;
    uint32_t uiNbPluginFolderLocations = pSubsystemPluginFolders->getNbChildren();

    if (!uiNbPluginFolderLocations) {

        strError = "Parameter Framework Configuration: couldn't find any PluginFolderLocation element";

        return false;
    }

    // Collect plugin paths
    for (uiPluginFolderLocation = 0; uiPluginFolderLocation < uiNbPluginFolderLocations; uiPluginFolderLocation++) {

        const CFrameworkConfigurationLocation* pSubsystemPluginLocation = static_cast<const CFrameworkConfigurationLocation*>(pSubsystemPluginFolders->getChild(uiPluginFolderLocation));

        _astrPluginFolderPaths.push_back(pSubsystemPluginLocation->getFilePath(_strXmlConfigurationFilePath));
    }

    // Log tuning availability
    log("Tuning %s", getConstFrameworkConfiguration()->isTuningAllowed() ? "allowed" : "prohibited");

    return true;
}

bool CParameterMgr::loadStructure(string& strError)
{
    // Retrieve system to load structure to
    CSystemClass* pSystemClass = getSystemClass();

    CAutoLog autoLog(this, "Loading " + pSystemClass->getName() + " system class structure");

    // Get structure description element
    const CFrameworkConfigurationLocation* pStructureDescriptionFileLocation = static_cast<const CFrameworkConfigurationLocation*>(getConstFrameworkConfiguration()->findChildOfKind("StructureDescriptionFileLocation"));

    if (!pStructureDescriptionFileLocation) {

        strError = "No StructureDescriptionFileLocation element found for SystemClass " + pSystemClass->getName();

        return false;
    }

    // Get Xml structure folder
    string strXmlStructureFolder = pStructureDescriptionFileLocation->getFolderPath(_strXmlConfigurationFolderPath);

    // Get Xml structure file name
    string strXmlStructureFilePath = pStructureDescriptionFileLocation->getFilePath(_strXmlConfigurationFolderPath);

    // Parse Structure XML file
    CXmlParameterSerializingContext parameterBuildContext(strError);

    log("Importing system structure from file %s", strXmlStructureFilePath.c_str());

    if (!xmlParse(parameterBuildContext, pSystemClass, strXmlStructureFilePath, strXmlStructureFolder, EParameterCreationLibrary)) {

        return false;
    }

    // Initialize offsets
    pSystemClass->setOffset(0);

    // Initialize main blackboard's size
    _pMainParameterBlackboard->setSize(pSystemClass->getFootPrint());

    return true;
}

bool CParameterMgr::loadSettings(string& strError)
{
    CAutoLog autoLog(this, "Loading settings");

    // Get settings configuration element
    const CFrameworkConfigurationGroup* pParameterConfigurationGroup = static_cast<const CFrameworkConfigurationGroup*>(getConstFrameworkConfiguration()->findChildOfKind("SettingsConfiguration"));

    if (!pParameterConfigurationGroup) {

        // No settings to load

        return true;
    }
    // Get binary settings file location
    const CFrameworkConfigurationLocation* pBinarySettingsFileLocation = static_cast<const CFrameworkConfigurationLocation*>(pParameterConfigurationGroup->findChildOfKind("BinarySettingsFileLocation"));

    string strXmlBinarySettingsFilePath;

    if (pBinarySettingsFileLocation) {

        // Get Xml binary settings file name
        strXmlBinarySettingsFilePath = pBinarySettingsFileLocation->getFilePath(_strXmlConfigurationFolderPath);
    }

    // Get configurable domains element
    const CFrameworkConfigurationLocation* pConfigurableDomainsFileLocation = static_cast<const CFrameworkConfigurationLocation*>(pParameterConfigurationGroup->findChildOfKind("ConfigurableDomainsFileLocation"));

    if (!pConfigurableDomainsFileLocation) {

        strError = "No ConfigurableDomainsFileLocation element found for SystemClass " + getSystemClass()->getName();

        return false;
    }    
    // Get destination root element
    CConfigurableDomains* pConfigurableDomains = getConfigurableDomains();

    // Get Xml configuration domains file name
    string strXmlConfigurationDomainsFilePath = pConfigurableDomainsFileLocation->getFilePath(_strXmlConfigurationFolderPath);

    // Get Xml configuration domains folder
    string strXmlConfigurationDomainsFolder = pConfigurableDomainsFileLocation->getFolderPath(_strXmlConfigurationFolderPath);

    // Parse configuration domains XML file (ask to read settings from XML file if they are not provided as binary)
    CXmlDomainSerializingContext xmlDomainSerializingContext(strError, !pBinarySettingsFileLocation);

    // Selection criteria definition for rule creation
    xmlDomainSerializingContext.setSelectionCriteriaDefinition(getConstSelectionCriteria()->getSelectionCriteriaDefinition());

    // Auto validation of configurations if no binary settings provided
    xmlDomainSerializingContext.setAutoValidationRequired(!pBinarySettingsFileLocation);

    log("Importing configurable domains from file %s %s settings", strXmlConfigurationDomainsFilePath.c_str(), pBinarySettingsFileLocation ? "without" : "with");

    // Do parse
    if (!xmlParse(xmlDomainSerializingContext, pConfigurableDomains, strXmlConfigurationDomainsFilePath, strXmlConfigurationDomainsFolder, EParameterConfigurationLibrary, "SystemClassName")) {

        return false;
    }
    // We have loaded the whole system structure, compute checksum
    const CSystemClass* pSystemClass = getConstSystemClass();
    _uiStructureChecksum = pSystemClass->computeStructureChecksum() + getConfigurableDomains()->computeStructureChecksum() + getSelectionCriteria()->computeStructureChecksum();

    // Load binary settings if any provided
    if (pBinarySettingsFileLocation && !pConfigurableDomains->serializeSettings(strXmlBinarySettingsFilePath, false, _uiStructureChecksum, strError)) {

        return false;
    }

    return true;
}

// XML parsing
bool CParameterMgr::xmlParse(CXmlElementSerializingContext& elementSerializingContext, CElement* pRootElement, const string& strXmlFilePath, const string& strXmlFolder, CParameterMgr::ElementLibrary eElementLibrary, const string& strNameAttrituteName)
{
    // Init serializing context
    elementSerializingContext.set(_pElementLibrarySet->getElementLibrary(eElementLibrary), strXmlFolder, _strSchemaFolderLocation);

    // Get Schema file associated to root element
    string strXmlSchemaFilePath = _strSchemaFolderLocation + "/" + pRootElement->getKind() + ".xsd";

    // Parse Structure XML file
    CXmlParser parser(strXmlFilePath, strXmlSchemaFilePath, pRootElement->getKind(), elementSerializingContext);

    if (!parser.open()) {

        return false;
    }

    // Check Root element name attribute (if any)
    string strRootElementName = parser.getRootElementAttributeString(strNameAttrituteName);

    if (!strRootElementName.empty() && strRootElementName != pRootElement->getName()) {

        elementSerializingContext.setError("Error: Wrong XML structure file " + strXmlFilePath);
        elementSerializingContext.appendLineToError(pRootElement->getKind() + " element " + pRootElement->getName() + " mismatches expected " + pRootElement->getKind() + " type " + pRootElement->getName());

        return false;
    }

    // Start clean
    pRootElement->clean();

    // Parse
    if (!parser.parse(pRootElement)) {

        // Cleanup
        pRootElement->clean();

        return false;
    }

    // Close parser
    if (!parser.close()) {

        return false;
    }

    return true;
}

// Init
bool CParameterMgr::init(string& strError)
{
    return base::init(strError);
}

// Selection criteria interface
CSelectionCriterionType* CParameterMgr::createSelectionCriterionType(bool bIsInclusive)
{
    // Propagate
    return getSelectionCriteria()->createSelectionCriterionType(bIsInclusive);
}

CSelectionCriterion* CParameterMgr::createSelectionCriterion(const string& strName, const CSelectionCriterionType* pSelectionCriterionType)
{
    // Propagate
    return getSelectionCriteria()->createSelectionCriterion(strName, pSelectionCriterionType);
}

// Selection criterion retrieval
CSelectionCriterion* CParameterMgr::getSelectionCriterion(const string& strName)
{
    // Propagate
    return getSelectionCriteria()->getSelectionCriterion(strName);
}

// Selection criteria changed event
bool CParameterMgr::applyConfigurations(string& strError)
{
    CAutoLog autoLog(this, "Configuration application request");

    // Lock state
    CAutoLock autoLock(&_blackboardMutex);

    if (!_bTuningModeIsOn) {

        // Apply configuration(s)
        if (!getConfigurableDomains()->apply(_pMainParameterBlackboard, false, strError)) {

            log("Failed to apply configurations!");

            return false;
        }
    }

    return true;
}

// Dynamic parameter handling
CParameterHandle* CParameterMgr::createParameterHandle(const string& strPath, string& strError)
{
    CPathNavigator pathNavigator(strPath);

    // Nagivate through system class
    if (!pathNavigator.navigateThrough(getConstSystemClass()->getName(), strError)) {

        return NULL;
    }

    // Find element
    const CElement* pElement = getConstSystemClass()->findDescendant(pathNavigator);

    if (!pElement) {

        strError = "Path not found";

        return NULL;
    }

    // Check found element is a parameter
    const CConfigurableElement* pConfigurableElement = static_cast<const CConfigurableElement*>(pElement);

    if (!pConfigurableElement->isParameter()) {

        // Element is not parameter
        strError = "Not a parameter";

        return NULL;
    }

    // Convert as parameter and return new handle
    return new CParameterHandle(static_cast<const CBaseParameter*>(pElement), this);
}

/////////////////// Remote command parsers
/// Version
CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::versionCommandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    (void)remoteCommand;

    // Show version
    strResult = getVersion();

    return CCommandHandler::ESucceeded;
}

/// Status
CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::statusCommandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    (void)remoteCommand;
    // System class
    const CSystemClass* pSystemClass = getSystemClass();

    // Show status
    /// General section
    appendTitle(strResult, "General:");
    // System class
    strResult += "System Class: ";
    strResult += pSystemClass->getName();
    strResult += "\n";

    // Tuning mode
    strResult += "Tuning Mode: ";
    strResult += tuningModeOn() ? "on" : "off";
    strResult += "\n";

    // Value space
    strResult += "Value Space: ";
    strResult += valueSpaceIsRaw() ? "raw" : "real";
    strResult += "\n";

    // Output raw format
    strResult += "Output Raw Format: ";
    strResult += outputRawFormatIsHex() ? "hex" : "dec";
    strResult += "\n";

    // Auto Sync
    strResult += "Auto Sync: ";
    strResult += autoSyncOn() ? "on" : "off";
    strResult += "\n";

    /// Subsystem list
    appendTitle(strResult, "Subsystems:");
    string strSubsystemList;
    pSystemClass->listChildrenPaths(strSubsystemList);
    strResult += strSubsystemList;

    /// Last applied configurations
    appendTitle(strResult, "Last applied configurations:");
    string strLastAppliedConfigurations;
    getConfigurableDomains()->listLastAppliedConfigurations(strLastAppliedConfigurations);
    strResult += strLastAppliedConfigurations;

    /// Criteria states
    appendTitle(strResult, "Selection criteria:");
    string strSelectionCriteria;
    getSelectionCriteria()->listSelectionCriteria(strSelectionCriteria, false);
    strResult += strSelectionCriteria;

    return CCommandHandler::ESucceeded;
}

/// Tuning Mode
CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::setTuningModeCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    if (remoteCommand.getArgument(0) == "on") {

        if (setTuningMode(true, strResult)) {

            return CCommandHandler::EDone;
        }
    } else if (remoteCommand.getArgument(0) == "off") {

        if (setTuningMode(false, strResult)) {

            return CCommandHandler::EDone;
        }
    } else {
        // Show usage
        return CCommandHandler::EShowUsage;
    }
    return CCommandHandler::EFailed;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::getTuningModeCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    (void)remoteCommand;

    strResult = tuningModeOn() ? "on" : "off";

    return CCommandHandler::ESucceeded;
}

/// Value Space
CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::setValueSpaceCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    (void)strResult;

    if (remoteCommand.getArgument(0) == "raw") {

        setValueSpace(true);

        return CCommandHandler::EDone;

    } else if (remoteCommand.getArgument(0) == "real") {

        setValueSpace(false);

        return CCommandHandler::EDone;

    } else {
        // Show usage
        return CCommandHandler::EShowUsage;
    }
    return CCommandHandler::EFailed;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::getValueSpaceCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    (void)remoteCommand;

    strResult = valueSpaceIsRaw() ? "raw" : "real";

    return CCommandHandler::ESucceeded;
}

/// Output Raw Format
CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::setOutputRawFormatCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    (void)strResult;

    if (remoteCommand.getArgument(0) == "hex") {

        setOutputRawFormat(true);

        return CCommandHandler::EDone;

    } else if (remoteCommand.getArgument(0) == "dec") {

        setOutputRawFormat(false);

        return CCommandHandler::EDone;

    } else {
        // Show usage
        return CCommandHandler::EShowUsage;
    }
    return CCommandHandler::EFailed;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::getOutputRawFormatCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    (void)remoteCommand;

    strResult = outputRawFormatIsHex() ? "hex" : "dec";

    return CCommandHandler::ESucceeded;
}

/// Sync
CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::setAutoSyncCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    if (remoteCommand.getArgument(0) == "on") {

        if (setAutoSync(true, strResult)) {

            return CCommandHandler::EDone;
        }
    } else if (remoteCommand.getArgument(0) == "off") {

        if (setAutoSync(false, strResult)) {

            return CCommandHandler::EDone;
        }
    } else {
        // Show usage
        return CCommandHandler::EShowUsage;
    }
    return CCommandHandler::EFailed;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::getAutoSyncCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    (void)remoteCommand;

    strResult = autoSyncOn() ? "on" : "off";

    return CCommandHandler::ESucceeded;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::syncCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    (void)remoteCommand;

    return sync(strResult) ? CCommandHandler::EDone : CCommandHandler::EFailed;
}

/// Criteria
CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::listCriteriaCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    (void)remoteCommand;

    getSelectionCriteria()->listSelectionCriteria(strResult, true);

    return CCommandHandler::ESucceeded;
}

/// Domains
CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::listDomainsCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    (void)remoteCommand;

    getConfigurableDomains()->listDomains(strResult);

    return CCommandHandler::ESucceeded;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::createDomainCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    return createDomain(remoteCommand.getArgument(0), strResult) ? CCommandHandler::EDone : CCommandHandler::EFailed;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::deleteDomainCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    return deleteDomain(remoteCommand.getArgument(0), strResult) ? CCommandHandler::EDone : CCommandHandler::EFailed;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::renameDomainCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    return getConfigurableDomains()->renameDomain(remoteCommand.getArgument(0), remoteCommand.getArgument(1), strResult) ? CCommandHandler::EDone : CCommandHandler::EFailed;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::setSequenceAwarenessCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    // Check tuning mode
    if (!checkTuningModeOn(strResult)) {

        return CCommandHandler::EFailed;
    }

    // Set property
    bool bSequenceAware;

    if (remoteCommand.getArgument(1) == "true") {

        bSequenceAware = true;

    } else if (remoteCommand.getArgument(1) == "false") {

        bSequenceAware = false;

    } else {
        // Show usage
        return CCommandHandler::EShowUsage;
    }

    return getConfigurableDomains()->setSequenceAwareness(remoteCommand.getArgument(0), bSequenceAware, strResult) ? CCommandHandler::EDone : CCommandHandler::EFailed;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::getSequenceAwarenessCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    // Get property
    bool bSequenceAware;

    if (!getConfigurableDomains()->getSequenceAwareness(remoteCommand.getArgument(0), bSequenceAware, strResult)) {

        return CCommandHandler::EFailed;
    }

    strResult = bSequenceAware ? "true" : "false";

    return CCommandHandler::ESucceeded;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::listDomainElementsCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    return getConfigurableDomains()->listDomainElements(remoteCommand.getArgument(0), strResult) ? CCommandHandler::ESucceeded : CCommandHandler::EFailed;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::addElementCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    return addConfigurableElementToDomain(remoteCommand.getArgument(0), remoteCommand.getArgument(1), strResult) ? CCommandHandler::EDone : CCommandHandler::EFailed;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::removeElementCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    return removeConfigurableElementFromDomain(remoteCommand.getArgument(0), remoteCommand.getArgument(1), strResult) ? CCommandHandler::EDone : CCommandHandler::EFailed;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::splitDomainCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    return split(remoteCommand.getArgument(0), remoteCommand.getArgument(1), strResult) ? CCommandHandler::EDone : CCommandHandler::EFailed;
}

/// Configurations
CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::listConfigurationsCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    return getConstConfigurableDomains()->listConfigurations(remoteCommand.getArgument(0), strResult) ? CCommandHandler::ESucceeded : CCommandHandler::EFailed;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::dumpDomainsCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    (void)remoteCommand;

    // Dummy error context
    string strError;
    CErrorContext errorContext(strError);

    // Dump
    getConstConfigurableDomains()->dumpContent(strResult, errorContext);

    return CCommandHandler::ESucceeded;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::createConfigurationCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    return createConfiguration(remoteCommand.getArgument(0), remoteCommand.getArgument(1), strResult) ? CCommandHandler::EDone : CCommandHandler::EFailed;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::deleteConfigurationCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    return deleteConfiguration(remoteCommand.getArgument(0), remoteCommand.getArgument(1), strResult) ? CCommandHandler::EDone : CCommandHandler::EFailed;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::renameConfigurationCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    return getConfigurableDomains()->renameConfiguration(remoteCommand.getArgument(0), remoteCommand.getArgument(1), remoteCommand.getArgument(2), strResult) ? CCommandHandler::EDone : CCommandHandler::EFailed;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::saveConfigurationCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    return saveConfiguration(remoteCommand.getArgument(0), remoteCommand.getArgument(1), strResult) ? CCommandHandler::EDone : CCommandHandler::EFailed;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::restoreConfigurationCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    return restoreConfiguration(remoteCommand.getArgument(0), remoteCommand.getArgument(1), strResult) ? CCommandHandler::EDone : CCommandHandler::EFailed;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::setElementSequenceCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    // Check tuning mode
    if (!checkTuningModeOn(strResult)) {

        return CCommandHandler::EFailed;
    }

    // Build configurable element path list
    vector<string> astrNewElementSequence;

    uint32_t uiArgument;

    for (uiArgument = 2; uiArgument < remoteCommand.getArgumentCount(); uiArgument++) {

        astrNewElementSequence.push_back(remoteCommand.getArgument(uiArgument));
    }

    // Delegate to configurable domains
    return getConfigurableDomains()->setElementSequence(remoteCommand.getArgument(0), remoteCommand.getArgument(1), astrNewElementSequence, strResult) ? CCommandHandler::EDone : CCommandHandler::EFailed;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::getElementSequenceCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    // Delegate to configurable domains
    return getConfigurableDomains()->getElementSequence(remoteCommand.getArgument(0), remoteCommand.getArgument(1), strResult) ? CCommandHandler::ESucceeded : CCommandHandler::EFailed;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::setRuleCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    // Delegate to configurable domains
    return getConfigurableDomains()->setApplicationRule(remoteCommand.getArgument(0), remoteCommand.getArgument(1), remoteCommand.packArguments(2, remoteCommand.getArgumentCount() - 2), getConstSelectionCriteria()->getSelectionCriteriaDefinition(), strResult) ? CCommandHandler::EDone : CCommandHandler::EFailed;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::clearRuleCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    // Delegate to configurable domains
    return getConfigurableDomains()->clearApplicationRule(remoteCommand.getArgument(0), remoteCommand.getArgument(1), strResult) ? CCommandHandler::EDone : CCommandHandler::EFailed;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::getRuleCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    // Delegate to configurable domains
    return getConfigurableDomains()->getApplicationRule(remoteCommand.getArgument(0), remoteCommand.getArgument(1), strResult) ? CCommandHandler::ESucceeded : CCommandHandler::EFailed;
}

/// Elements/Parameters
CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::listElementsCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    CElementLocator elementLocator(getSystemClass(), false);

    CElement* pLocatedElement = NULL;

    if (!elementLocator.locate(remoteCommand.getArgument(0), &pLocatedElement, strResult)) {

        return CCommandHandler::EFailed;
    }

    strResult = string("\n");

    if (!pLocatedElement) {

        // List from root folder

        // Return system class qualified name
        pLocatedElement = getSystemClass();
    }

    // Return sub-elements
    strResult += pLocatedElement->listQualifiedPaths(false);

    return CCommandHandler::ESucceeded;
}

/// Elements/Parameters
CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::listParametersCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    CElementLocator elementLocator(getSystemClass(), false);

    CElement* pLocatedElement = NULL;

    if (!elementLocator.locate(remoteCommand.getArgument(0), &pLocatedElement, strResult)) {

        return CCommandHandler::EFailed;
    }

    strResult = string("\n");

    if (!pLocatedElement) {

        // List from root folder

        // Return system class qualified name
        pLocatedElement = getSystemClass();
    }

    // Return sub-elements
    strResult += pLocatedElement->listQualifiedPaths(true);

    return CCommandHandler::ESucceeded;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::dumpElementCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    CElementLocator elementLocator(getSystemClass());

    CElement* pLocatedElement = NULL;

    if (!elementLocator.locate(remoteCommand.getArgument(0), &pLocatedElement, strResult)) {

        return CCommandHandler::EFailed;
    }

    string strError;

    CParameterAccessContext parameterAccessContext(strError, _pMainParameterBlackboard, _bValueSpaceIsRaw, _bOutputRawFormatIsHex);

    // Dump elements
    pLocatedElement->dumpContent(strResult, parameterAccessContext);

    return CCommandHandler::ESucceeded;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::getElementSizeCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    CElementLocator elementLocator(getSystemClass());

    CElement* pLocatedElement = NULL;

    if (!elementLocator.locate(remoteCommand.getArgument(0), &pLocatedElement, strResult)) {

        return CCommandHandler::EFailed;
    }

    // Converted to actual sizable element
    const CConfigurableElement* pConfigurableElement = static_cast<const CConfigurableElement*>(pLocatedElement);

    // Get size as string
    strResult = pConfigurableElement->getFootprintAsString();

    return CCommandHandler::ESucceeded;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::showPropertiesCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    CElementLocator elementLocator(getSystemClass());

    CElement* pLocatedElement = NULL;

    if (!elementLocator.locate(remoteCommand.getArgument(0), &pLocatedElement, strResult)) {

        return CCommandHandler::EFailed;
    }

    // Convert element
    const CConfigurableElement* pConfigurableElement = static_cast<const CConfigurableElement*>(pLocatedElement);

    // Return element properties
    pConfigurableElement->showProperties(strResult);

    return CCommandHandler::ESucceeded;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::getParameterCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    string strValue;

    if (!accessValue(remoteCommand.getArgument(0), strValue, false, strResult)) {

        return CCommandHandler::EFailed;
    }
    // Succeeded
    strResult = strValue;

    return CCommandHandler::ESucceeded;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::setParameterCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    // Check tuning mode
    if (!checkTuningModeOn(strResult)) {

        return CCommandHandler::EFailed;
    }
    // Get value to set
    string strValue = remoteCommand.packArguments(1, remoteCommand.getArgumentCount() - 1);

    return accessValue(remoteCommand.getArgument(0), strValue, true, strResult) ? CCommandHandler::EDone : CCommandHandler::EFailed;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::listBelongingDomainsCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    CElementLocator elementLocator(getSystemClass());

    CElement* pLocatedElement = NULL;

    if (!elementLocator.locate(remoteCommand.getArgument(0), &pLocatedElement, strResult)) {

        return CCommandHandler::EFailed;
    }

    // Convert element
    const CConfigurableElement* pConfigurableElement = static_cast<const CConfigurableElement*>(pLocatedElement);

    // Return element belonging domains
    pConfigurableElement->listBelongingDomains(strResult);

    return CCommandHandler::ESucceeded;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::listAssociatedDomainsCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    CElementLocator elementLocator(getSystemClass());

    CElement* pLocatedElement = NULL;

    if (!elementLocator.locate(remoteCommand.getArgument(0), &pLocatedElement, strResult)) {

        return CCommandHandler::EFailed;
    }

    // Convert element
    const CConfigurableElement* pConfigurableElement = static_cast<const CConfigurableElement*>(pLocatedElement);

    // Return element belonging domains
    pConfigurableElement->listAssociatedDomains(strResult);

    return CCommandHandler::ESucceeded;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::listAssociatedElementsCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    (void)remoteCommand;

    getConfigurableDomains()->listAssociatedElements(strResult);

    return CCommandHandler::ESucceeded;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::listConflictingElementsCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    (void)remoteCommand;

    getConfigurableDomains()->listConflictingElements(strResult);

    return CCommandHandler::ESucceeded;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::listRogueElementsCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    (void)remoteCommand;

    getSystemClass()->listRogueElements(strResult);

    return CCommandHandler::ESucceeded;
}

/// Settings Import/Export
CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::exportConfigurableDomainsToXMLCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    return exportDomainsXml(remoteCommand.getArgument(0), false, strResult) ? CCommandHandler::EDone : CCommandHandler::EFailed;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::importConfigurableDomainsFromXMLCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    return importDomainsXml(remoteCommand.getArgument(0), false, strResult) ? CCommandHandler::EDone : CCommandHandler::EFailed;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::exportConfigurableDomainsWithSettingsToXMLCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    return exportDomainsXml(remoteCommand.getArgument(0), true, strResult) ? CCommandHandler::EDone : CCommandHandler::EFailed;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::importConfigurableDomainsWithSettingsFromXMLCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    return importDomainsXml(remoteCommand.getArgument(0), true, strResult) ? CCommandHandler::EDone : CCommandHandler::EFailed;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::exportSettingsCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    return exportDomainsBinary(remoteCommand.getArgument(0), strResult) ? CCommandHandler::EDone : CCommandHandler::EFailed;
}

CParameterMgr::CCommandHandler::CommandStatus CParameterMgr::importSettingsCommmandProcess(const IRemoteCommand& remoteCommand, string& strResult)
{
    return importDomainsBinary(remoteCommand.getArgument(0), strResult) ? CCommandHandler::EDone : CCommandHandler::EFailed;
}

// User set/get parameters
bool CParameterMgr::accessValue(const string& strPath, string& strValue, bool bSet, string& strError)
{
    // Lock state
    CAutoLock autoLock(&_blackboardMutex);

    CPathNavigator pathNavigator(strPath);

    // Nagivate through system class
    if (!pathNavigator.navigateThrough(getConstSystemClass()->getName(), strError)) {

        return false;
    }

    // Define context
    CParameterAccessContext parameterAccessContext(strError, _pMainParameterBlackboard, _bValueSpaceIsRaw, _bOutputRawFormatIsHex);

    // Auto Sync
    if (bSet) {

        parameterAccessContext.setAutoSync(_bAutoSyncOn);
    }

    // Do the get
    return getConstSystemClass()->accessValue(pathNavigator, strValue, bSet, parameterAccessContext);
}

// Tuning mode
bool CParameterMgr::setTuningMode(bool bOn, string& strError)
{
    // Tuning allowed?
    if (bOn && !getConstFrameworkConfiguration()->isTuningAllowed()) {

        strError = "Tuning prohibited";

        return false;
    }
    // Lock state
    CAutoLock autoLock(&_blackboardMutex);

    // Warn domains about exiting tuning mode
    if (!bOn && _bTuningModeIsOn) {

        // Ensure application of currently selected configurations
        // Force-apply configurations
        if (!getConfigurableDomains()->apply(_pMainParameterBlackboard, true, strError)) {

            return false;
        }
        // Turn auto sync back on
        _bAutoSyncOn = true;
    }

    // Store
    _bTuningModeIsOn = bOn;

    return true;
}

bool CParameterMgr::tuningModeOn() const
{
    return _bTuningModeIsOn;
}

// Current value space for user set/get value interpretation
void CParameterMgr::setValueSpace(bool bIsRaw)
{
    _bValueSpaceIsRaw = bIsRaw;
}

bool CParameterMgr::valueSpaceIsRaw()
{
    return _bValueSpaceIsRaw;
}

// Current Output Raw Format for user get value interpretation
void CParameterMgr::setOutputRawFormat(bool bIsHex)
{
    _bOutputRawFormatIsHex = bIsHex;
}

bool CParameterMgr::outputRawFormatIsHex()
{
    return _bOutputRawFormatIsHex;
}

/// Sync
// Automatic hardware synchronization control (during tuning session)
bool CParameterMgr::setAutoSync(bool bAutoSyncOn, string& strError)
{
    // Check tuning mode
    if (!checkTuningModeOn(strError)) {

        return false;
    }
    // Warn domains about turning auto sync back on
    if (bAutoSyncOn && !_bAutoSyncOn) {

        // Do the synchronization at system class level (could be optimized by keeping track of all modified parameters)
        if (!sync(strError)) {

            return false;
        }
    }

    // Set Auto sync
    _bAutoSyncOn = bAutoSyncOn;

    return true;
}

bool CParameterMgr::autoSyncOn() const
{
    return _bAutoSyncOn;
}

// Manual hardware synchronization control (during tuning session)
bool CParameterMgr::sync(string& strError)
{
    // Check tuning mode
    if (!checkTuningModeOn(strError)) {

        return false;
    }
    // Warn domains about turning auto sync back on
    if (_bAutoSyncOn) {

        strError = "Feature unavailable when Auto Sync is on";

        return false;
    }

    // Get syncer set
    CSyncerSet syncerSet;
    // ... from system class
    getConstSystemClass()->fillSyncerSet(syncerSet);
    // Sync
    return syncerSet.sync(*_pMainParameterBlackboard, false, strError);
}

// Content dump
void CParameterMgr::logStructureContent(string& strContent) const
{
    string strError;

    CParameterAccessContext parameterAccessContext(strError, _pMainParameterBlackboard, _bValueSpaceIsRaw, _bOutputRawFormatIsHex);

    dumpContent(strContent, parameterAccessContext);
}

// Configuration/Domains handling
bool CParameterMgr::createDomain(const string& strName, string& strError)
{
    // Check tuning mode
    if (!checkTuningModeOn(strError)) {

        return false;
    }

    // Delegate to configurable domains
    return getConfigurableDomains()->createDomain(strName, strError);
}

bool CParameterMgr::deleteDomain(const string& strName, string& strError)
{
    // Check tuning mode
    if (!checkTuningModeOn(strError)) {

        return false;
    }

    // Delegate to configurable domains
    return getConfigurableDomains()->deleteDomain(strName, strError);
}

bool CParameterMgr::createConfiguration(const string& strDomain, const string& strConfiguration, string& strError)
{
    // Check tuning mode
    if (!checkTuningModeOn(strError)) {

        return false;
    }

    // Delegate to configurable domains
    return getConfigurableDomains()->createConfiguration(strDomain, strConfiguration, _pMainParameterBlackboard, strError);
}

bool CParameterMgr::deleteConfiguration(const string& strDomain, const string& strConfiguration, string& strError)
{
    // Check tuning mode
    if (!checkTuningModeOn(strError)) {

        return false;
    }

    // Delegate to configurable domains
    return getConfigurableDomains()->deleteConfiguration(strDomain, strConfiguration, strError);
}

bool CParameterMgr::restoreConfiguration(const string& strDomain, const string& strConfiguration, string& strError)
{
    // Check tuning mode
    if (!checkTuningModeOn(strError)) {

        return false;
    }

    // Delegate to configurable domains
    return getConstConfigurableDomains()->restoreConfiguration(strDomain, strConfiguration, _pMainParameterBlackboard, _bAutoSyncOn, strError);
}

bool CParameterMgr::saveConfiguration(const string& strDomain, const string& strConfiguration, string& strError)
{
    // Check tuning mode
    if (!checkTuningModeOn(strError)) {

        return false;
    }

    // Delegate to configurable domains
    return getConfigurableDomains()->saveConfiguration(strDomain, strConfiguration, _pMainParameterBlackboard, strError);
}

// Configurable element - domain association
bool CParameterMgr::addConfigurableElementToDomain(const string& strDomain, const string& strConfigurableElementPath, string& strError)
{
    // Check tuning mode
    if (!checkTuningModeOn(strError)) {

        return false;
    }

    CElementLocator elementLocator(getSystemClass());

    CElement* pLocatedElement = NULL;

    if (!elementLocator.locate(strConfigurableElementPath, &pLocatedElement, strError)) {

        return false;
    }

    // Convert element
    CConfigurableElement* pConfigurableElement = static_cast<const CConfigurableElement*>(pLocatedElement);

    // Delegate
    return getConfigurableDomains()->addConfigurableElementToDomain(strDomain, pConfigurableElement, _pMainParameterBlackboard, strError);
}

bool CParameterMgr::removeConfigurableElementFromDomain(const string& strDomain, const string& strConfigurableElementPath, string& strError)
{
    // Check tuning mode
    if (!checkTuningModeOn(strError)) {

        return false;
    }

    CElementLocator elementLocator(getSystemClass());

    CElement* pLocatedElement = NULL;

    if (!elementLocator.locate(strConfigurableElementPath, &pLocatedElement, strError)) {

        return CCommandHandler::EFailed;
    }

    // Convert element
    CConfigurableElement* pConfigurableElement = static_cast<const CConfigurableElement*>(pLocatedElement);

    // Delegate
    return getConfigurableDomains()->removeConfigurableElementFromDomain(strDomain, pConfigurableElement, strError);
}

bool CParameterMgr::split(const string& strDomain, const string& strConfigurableElementPath, string& strError)
{
    // Check tuning mode
    if (!checkTuningModeOn(strError)) {

        return false;
    }

    CElementLocator elementLocator(getSystemClass());

    CElement* pLocatedElement = NULL;

    if (!elementLocator.locate(strConfigurableElementPath, &pLocatedElement, strError)) {

        return CCommandHandler::EFailed;
    }

    // Convert element
    CConfigurableElement* pConfigurableElement = static_cast<const CConfigurableElement*>(pLocatedElement);

    // Delegate
    return getConfigurableDomains()->split(strDomain, pConfigurableElement, strError);
}

// XML Import/Export
bool CParameterMgr::importDomainsXml(const string& strFileName, bool bWithSettings, string& strError)
{
    // Check tuning mode
    if (!checkTuningModeOn(strError)) {

        return false;
    }

    // check path is absolute
    if (strFileName[0] != '/') {

        strError = "Please provide absolute path";

        return false;
    }
    // Root element
    CConfigurableDomains* pConfigurableDomains = getConfigurableDomains();

    // Context
    CXmlDomainSerializingContext xmlDomainSerializingContext(strError, bWithSettings);

    // Secltion criteria definition for rule creation
    xmlDomainSerializingContext.setSelectionCriteriaDefinition(getConstSelectionCriteria()->getSelectionCriteriaDefinition());

    // Parse
    if (!xmlParse(xmlDomainSerializingContext, pConfigurableDomains, strFileName, "", EParameterConfigurationLibrary, "SystemClassName")) {

        return false;
    }

    // Validate domains after XML import
    pConfigurableDomains->validate(_pMainParameterBlackboard);

    return true;
}

bool CParameterMgr::exportDomainsXml(const string& strFileName, bool bWithSettings, string& strError) const
{
    // check path is absolute
    if (strFileName[0] != '/') {

        strError = "Please provide absolute path";

        return false;
    }

    // Root element
    const CConfigurableDomains* pConfigurableDomains = getConstConfigurableDomains();

    // Get Schema file associated to root element
    string strXmlSchemaFilePath = _strSchemaFolderLocation + "/" + pConfigurableDomains->getKind() + ".xsd";

    // Context
    CXmlDomainSerializingContext xmlDomainSerializingContext(strError, bWithSettings);

    // Value space
    xmlDomainSerializingContext.setValueSpaceRaw(_bValueSpaceIsRaw);

    // Output raw format
    xmlDomainSerializingContext.setOutputRawFormat(_bOutputRawFormatIsHex);

    // Instantiate composer
    CXmlComposer xmlComposer(strFileName, strXmlSchemaFilePath, pConfigurableDomains->getKind(), xmlDomainSerializingContext);

    // Open composer
    if (!xmlComposer.open()) {

        return false;
    }

    // Compose
    xmlComposer.compose(pConfigurableDomains, "parameter-framework", getVersion());

    // Close composer
    if (!xmlComposer.close()) {

        return false;
    }

    return true;
}

// Binary Import/Export
bool CParameterMgr::importDomainsBinary(const string& strFileName, string& strError)
{
    // Check tuning mode
    if (!checkTuningModeOn(strError)) {

        return false;
    }
    // check path is absolute
    if (strFileName[0] != '/') {

        strError = "Please provide absolute path";

        return false;
    }
    // Root element
    CConfigurableDomains* pConfigurableDomains = getConfigurableDomains();

    // Serialize in
    return pConfigurableDomains->serializeSettings(strFileName, false, _uiStructureChecksum, strError);
}

bool CParameterMgr::exportDomainsBinary(const string& strFileName, string& strError)
{
    // check path is absolute
    if (strFileName[0] != '/') {

        strError = "Please provide absolute path";

        return false;
    }

    // Root element
    CConfigurableDomains* pConfigurableDomains = getConfigurableDomains();

    // Serialize out
    return pConfigurableDomains->serializeSettings(strFileName, true, _uiStructureChecksum, strError);
}

// For tuning, check we're in tuning mode
bool CParameterMgr::checkTuningModeOn(string& strError) const
{
    // Tuning Mode on?
    if (!_bTuningModeIsOn) {

        strError = "Tuning Mode must be on";

        return false;
    }
    return true;
}

// Tuning mutex dynamic parameter handling
pthread_mutex_t* CParameterMgr::getBlackboardMutex()
{
    return &_blackboardMutex;
}

// Blackboard reference (dynamic parameter handling)
CParameterBlackboard* CParameterMgr::getParameterBlackboard()
{
    return _pMainParameterBlackboard;
}

// Dynamic creation library feeding
void CParameterMgr::feedElementLibraries()
{
    // Global Configuration handling
    CElementLibrary* pFrameworkConfigurationLibrary = new CElementLibrary;

    pFrameworkConfigurationLibrary->addElementBuilder(new TElementBuilderTemplate<CParameterFrameworkConfiguration>("ParameterFrameworkConfiguration"));
    pFrameworkConfigurationLibrary->addElementBuilder(new TKindElementBuilderTemplate<CFrameworkConfigurationGroup>("SubsystemPluginFolders"));
    pFrameworkConfigurationLibrary->addElementBuilder(new TKindElementBuilderTemplate<CFrameworkConfigurationLocation>("PluginFolderLocation"));
    pFrameworkConfigurationLibrary->addElementBuilder(new TKindElementBuilderTemplate<CFrameworkConfigurationLocation>("StructureDescriptionFileLocation"));
    pFrameworkConfigurationLibrary->addElementBuilder(new TKindElementBuilderTemplate<CFrameworkConfigurationGroup>("SettingsConfiguration"));
    pFrameworkConfigurationLibrary->addElementBuilder(new TKindElementBuilderTemplate<CFrameworkConfigurationLocation>("ConfigurableDomainsFileLocation"));
    pFrameworkConfigurationLibrary->addElementBuilder(new TKindElementBuilderTemplate<CFrameworkConfigurationLocation>("BinarySettingsFileLocation"));

    _pElementLibrarySet->addElementLibrary(pFrameworkConfigurationLibrary);

    // Parameter creation
    CElementLibrary* pParameterCreationLibrary = new CElementLibrary;

    pParameterCreationLibrary->addElementBuilder(new CSubsystemElementBuilder(getSystemClass()->getSubsystemLibrary()));
    pParameterCreationLibrary->addElementBuilder(new TNamedElementBuilderTemplate<CComponentType>("ComponentType"));
    pParameterCreationLibrary->addElementBuilder(new TNamedElementBuilderTemplate<CComponentInstance>("Component"));
    pParameterCreationLibrary->addElementBuilder(new TNamedElementBuilderTemplate<CBitParameterType>("BitParameter"));
    pParameterCreationLibrary->addElementBuilder(new TNamedElementBuilderTemplate<CBitParameterBlockType>("BitParameterBlock"));
    pParameterCreationLibrary->addElementBuilder(new TNamedElementBuilderTemplate<CStringParameterType>("StringParameter"));
    pParameterCreationLibrary->addElementBuilder(new TNamedElementBuilderTemplate<CParameterBlockType>("ParameterBlock"));
    pParameterCreationLibrary->addElementBuilder(new TNamedElementBuilderTemplate<CBooleanParameterType>("BooleanParameter"));
    pParameterCreationLibrary->addElementBuilder(new TNamedElementBuilderTemplate<CIntegerParameterType>("IntegerParameter"));
    pParameterCreationLibrary->addElementBuilder(new TElementBuilderTemplate<CLinearParameterAdaptation>("LinearAdaptation"));
    pParameterCreationLibrary->addElementBuilder(new TNamedElementBuilderTemplate<CEnumParameterType>("EnumParameter"));
    pParameterCreationLibrary->addElementBuilder(new TElementBuilderTemplate<CEnumValuePair>("ValuePair"));
    pParameterCreationLibrary->addElementBuilder(new TNamedElementBuilderTemplate<CFixedPointParameterType>("FixedPointParameter"));
    pParameterCreationLibrary->addElementBuilder(new TKindElementBuilderTemplate<CXmlFileIncluderElement>("SubsystemInclude"));

    _pElementLibrarySet->addElementLibrary(pParameterCreationLibrary);

    // Parameter Configuration Domains creation
    CElementLibrary* pParameterConfigurationLibrary = new CElementLibrary;

    pParameterConfigurationLibrary->addElementBuilder(new TNamedElementBuilderTemplate<CConfigurableDomain>("ConfigurableDomain"));
    pParameterConfigurationLibrary->addElementBuilder(new TNamedElementBuilderTemplate<CDomainConfiguration>("Configuration"));
    pParameterConfigurationLibrary->addElementBuilder(new TElementBuilderTemplate<CCompoundRule>("CompoundRule"));
    pParameterConfigurationLibrary->addElementBuilder(new TElementBuilderTemplate<CSelectionCriterionRule>("SelectionCriterionRule"));

    _pElementLibrarySet->addElementLibrary(pParameterConfigurationLibrary);
}

// Remote Processor Server connection handling
bool CParameterMgr::handleRemoteProcessingInterface(string& strError)
{
    CAutoLog autoLog(this, "Handling remote processing interface");

    // Start server if tuning allowed
    if (getConstFrameworkConfiguration()->isTuningAllowed()) {

        log("Loading remote processor library");

        // Load library
        void* lib_handle = dlopen("libremote-processor.so", RTLD_NOW);

        if (!lib_handle) {

            // Return error
            const char* pcError = dlerror();

            if (pcError) {

                strError = pcError;
            } else {

                strError = "Unable to load libremote-processor.so library";
            }

            return false;
        }

        CreateRemoteProcessorServer pfnCreateRemoteProcessorServer = (CreateRemoteProcessorServer)dlsym(lib_handle, "createRemoteProcessorServer");

        if (!pfnCreateRemoteProcessorServer) {

            strError = "libremote-process.so does not contain createRemoteProcessorServer symbol.";

            return false;
        }

        // Create server
        _pRemoteProcessorServer = pfnCreateRemoteProcessorServer(getConstFrameworkConfiguration()->getServerPort(), _pCommandHandler);

        log("Starting remote processor server on port %d", getConstFrameworkConfiguration()->getServerPort());
        // Start
        if (!_pRemoteProcessorServer->start()) {

            strError = "Unable to start remote processor server";

            return false;
        }
    }

    return true;
}

// Back synchronization
CBackSynchronizer* CParameterMgr::createBackSynchronizer(string& strError) const
{
#ifdef SIMULATION
    // In simulation, back synchronization of the blackboard won't probably work
    // We need to ensure though the blackboard is initialized with valid data
    return new CSimulatedBackSynchronizer(getConstSystemClass(), strError, _pMainParameterBlackboard);
#else
    // Real back synchronizer from subsystems
    return new CHardwareBackSynchronizer(getConstSystemClass(), strError, _pMainParameterBlackboard);
#endif
}

// Children typwise access
CParameterFrameworkConfiguration* CParameterMgr::getFrameworkConfiguration()
{
    return static_cast<CParameterFrameworkConfiguration*>(getChild(EFrameworkConfiguration));
}

const CParameterFrameworkConfiguration* CParameterMgr::getConstFrameworkConfiguration()
{
    return getFrameworkConfiguration();
}

CSelectionCriteria* CParameterMgr::getSelectionCriteria()
{
    return static_cast<CSelectionCriteria*>(getChild(ESelectionCriteria));
}

const CSelectionCriteria* CParameterMgr::getConstSelectionCriteria()
{
    return static_cast<const CSelectionCriteria*>(getChild(ESelectionCriteria));
}

CSystemClass* CParameterMgr::getSystemClass()
{
    return static_cast<CSystemClass*>(getChild(ESystemClass));
}

const CSystemClass* CParameterMgr::getConstSystemClass() const
{
    return static_cast<const CSystemClass*>(getChild(ESystemClass));
}

// Configurable Domains
CConfigurableDomains* CParameterMgr::getConfigurableDomains()
{
    return static_cast<CConfigurableDomains*>(getChild(EConfigurableDomains));
}

const CConfigurableDomains* CParameterMgr::getConstConfigurableDomains()
{
    return static_cast<const CConfigurableDomains*>(getChild(EConfigurableDomains));
}

const CConfigurableDomains* CParameterMgr::getConstConfigurableDomains() const
{
    return static_cast<const CConfigurableDomains*>(getChild(EConfigurableDomains));
}
