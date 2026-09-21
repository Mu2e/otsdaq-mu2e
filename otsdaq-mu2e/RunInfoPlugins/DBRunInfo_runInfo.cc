#include "otsdaq-mu2e/RunInfoPlugins/DBRunInfo.h"
#include "otsdaq/Macros/CoutMacros.h"
#include "otsdaq/Macros/RunInfoPluginMacros.h"
#include "otsdaq/MessageFacility/MessageFacility.h"

#include <libpq-fe.h> /* for PGconn */
#include <boost/algorithm/string.hpp>
#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

using namespace ots;

//==============================================================================
DBRunInfo::DBRunInfo(const std::string& runInfoPluginClassName,
                     const std::string& activeStateMachineName)
    : RunInfoVInterface(runInfoPluginClassName, activeStateMachineName)
{
	dbname_   = const_cast<char*>(getenv("OTSDAQ_RUNINFO_DATABASE")
	                                  ? getenv("OTSDAQ_RUNINFO_DATABASE")
	                                  : "run_info");
	dbhost_   = const_cast<char*>(getenv("OTSDAQ_RUNINFO_DATABASE_HOST")
	                                  ? getenv("OTSDAQ_RUNINFO_DATABASE_HOST")
	                                  : "");
	dbport_   = const_cast<char*>(getenv("OTSDAQ_RUNINFO_DATABASE_PORT")
	                                  ? getenv("OTSDAQ_RUNINFO_DATABASE_PORT")
	                                  : "");
	dbuser_   = const_cast<char*>(getenv("OTSDAQ_RUNINFO_DATABASE_USER")
	                                  ? getenv("OTSDAQ_RUNINFO_DATABASE_USER")
	                                  : "");
	dbpwd_    = const_cast<char*>(getenv("OTSDAQ_RUNINFO_DATABASE_PWD")
	                                  ? getenv("OTSDAQ_RUNINFO_DATABASE_PWD")
	                                  : "");
	dbSchema_ = const_cast<char*>(getenv("OTSDAQ_RUNINFO_DATABASE_SCHEMA")
	                                  ? getenv("OTSDAQ_RUNINFO_DATABASE_SCHEMA")
	                                  : "test");

	__COUTV__(getActiveStateMachineName());
	//open db connection
	openDbConnection();
}  //end constructor()

//==============================================================================
DBRunInfo::~DBRunInfo(void)
{
	if(runInfoDbConn_)
		PQfinish(runInfoDbConn_);
}

//==============================================================================
void DBRunInfo::openDbConnection()
{
	__COUT__ << "Opening Run Info db connection at " << dbhost_ << ":" << dbport_
	         << __E__;
	//open db connection
	char runInfoDbConnInfo[1024];
	sprintf(runInfoDbConnInfo,
	        "dbname=%s host=%s port=%s  \
		user=%s password=%s connect_timeout=10",
	        dbname_,
	        dbhost_,
	        dbport_,
	        dbuser_,
	        dbpwd_);
	runInfoDbConn_ = PQconnectdb(runInfoDbConnInfo);

	if(PQstatus(runInfoDbConn_) != CONNECTION_OK)
	{
		__SS__ << "Connection failed: " << PQerrorMessage(runInfoDbConn_) << std::endl;
		PQfinish(runInfoDbConn_);
		runInfoDbConn_ = nullptr;
		__SS_THROW__;
	}
	__COUT__ << "Run Info db connection opened successfully at " << dbhost_ << ":"
	         << dbport_ << __E__;
}  //end openDbConnection()

//==============================================================================
std::vector<std::string> DBRunInfo::getTableNames(const std::string& tableName)
{
	std::vector<std::string> names;

	char listBuffer[1024];
	snprintf(listBuffer,
	         sizeof(listBuffer),
	         "SELECT name FROM %s.%s ORDER BY name;",
	         dbSchema_,
	         tableName.c_str());

	PGresult* listRes = PQexec(runInfoDbConn_, listBuffer);

	if(PQresultStatus(listRes) == PGRES_TUPLES_OK && PQntuples(listRes) > 0)
	{
		for(int i = 0; i < PQntuples(listRes); i++)
		{
			names.push_back(PQgetvalue(listRes, i, 0));
		}
	}

	if(listRes)
		PQclear(listRes);

	return names;
}  //end getTableNames()

//==============================================================================
void DBRunInfo::appendNotFoundError(std::stringstream&              ss,
                                    const std::string&              providedName,
                                    const std::string&              tableName,
                                    const std::string&              entityDescription,
                                    const std::vector<std::string>& availableNames,
                                    const std::string&              additionalNote)
{
	ss << "The " << entityDescription << " '" << providedName
	   << "' does not match any entry in the " << tableName << " table." << __E__;

	if(!additionalNote.empty())
	{
		ss << additionalNote << __E__;
	}

	ss << "The " << entityDescription << " needs to match one of the following "
	   << tableName << " names:" << __E__;

	if(availableNames.empty())
	{
		ss << "  (No " << tableName << " entries found in the database)" << __E__;
	}
	else
	{
		for(size_t i = 0; i < availableNames.size(); i++)
		{
			ss << "  " << (i + 1) << ". " << availableNames[i] << __E__;
		}
	}
}  //end appendNotFoundError()

//==============================================================================
int DBRunInfo::checkAndReconnectDb(const std::string& operationDescription)
{
	int runInfoDbConnStatus_ = 0;

	if(PQstatus(runInfoDbConn_) == CONNECTION_BAD)
	{
		__COUT__ << "Unable to connect to the run_info database " << operationDescription
		         << "\n"
		         << __E__;
		PQfinish(runInfoDbConn_);
		runInfoDbConn_ = nullptr;

		//Try to open again the db connection
		openDbConnection();
		if(PQstatus(runInfoDbConn_) == CONNECTION_BAD)
		{
			__COUT__ << "Unable to connect for the second time to the run_info database "
			         << operationDescription << "\n"
			         << __E__;
			PQfinish(runInfoDbConn_);
			runInfoDbConn_ = nullptr;
		}
		else
		{
			__COUT__ << "Connected to the run_info database after a second tentative "
			         << operationDescription << "\n"
			         << __E__;
			runInfoDbConnStatus_ = 1;
		}
	}
	else
	{
		__COUT__ << "Connected to the run_info database " << operationDescription << "\n"
		         << __E__;
		runInfoDbConnStatus_ = 1;
	}

	return runInfoDbConnStatus_;
}  //end checkAndReconnectDb()

//==============================================================================
std::vector<std::vector<std::string>> DBRunInfo::convertResultToVector(PGresult* res)
{
	std::vector<std::vector<std::string>> records;

	if(PQntuples(res) >= 1)
	{
		int nFields = PQnfields(res);
		records.resize(PQntuples(res));

		for(int i = 0; i < PQntuples(res); i++)
		{
			records[i].resize(nFields);
			for(int j = 0; j < nFields; j++)
			{
				records[i][j] = PQgetvalue(res, i, j);
			}
		}
	}

	return records;
}  //end convertResultToVector()

//==============================================================================
/// insertRunCondition
///		Creates a new condition/config record in the database during the Configure transition.
///		This is called by the state machine when transitioning to Configured state.
///
///		@param runNumber - The run number associated with this run condition.
///		@param runConditionMap - The map of configuration dump (JSON format for Mu2e) containing all
///		                           configuration information of all subsystems (i.e. remote and local gateways).
///		                           The outer map key is the subsystem name. The inner map key is the field name (type/name/field)
///		                           and the inner map value is the corresponding value for that field.
///		@param configureConditionID - The database ID of the configure condition record created
///		                             during the configure transition. This is used to link
///		                             the run condition to the configure condition (if desired).
///		@param comment - A user comment associated with this run transition.
///		@return runConditionID - The database ID of the inserted run record. This is needed to link future run transitions to this run record.
unsigned int DBRunInfo::insertRunCondition(
    unsigned int runNumber,
    const std::map<std::string /* subsystem */,
                   std::map<std::string /*type/name/field */, std::string /* value */>>&
                 runConditionMap,
    unsigned int configureConditionID,
    const std::string& /* comment */)
{
	uint64_t conditionID = (unsigned int)-1;

	__COUT__ << "insert Run Condition" << __E__;

	// char* mu2eOwner = __ENV__("MU2E_OWNER");
	//char* hostName = __ENV__("HOSTNAME");

	int runInfoDbConnStatus_ = checkAndReconnectDb("inserting run condition");

	// write run condition into db
	if(runInfoDbConn_ && runInfoDbConnStatus_ == 1)
	{
		PGresult* res;

		// Iterate through each subsystem in the map
		for(auto const& [subsystem, fieldMap] : runConditionMap)
		{
			nlohmann::json jsonObj;
			for(auto const& [field, value] : fieldMap)
			{
				// first non-whitespace character
				size_t firstRealChar = value.find_first_not_of(" \t\n\r");

				if(firstRealChar != std::string::npos &&
				   (value[firstRealChar] == '{' || value[firstRealChar] == '['))
				{
					const char* scratchEnv = std::getenv("OTS_SCRATCH");
					std::string fullPath2;
					fullPath2 = std::string(scratchEnv) + "/Logs/runlog_dump.txt";
					std::ofstream debugFile2(fullPath2, std::ios::out);

					if(debugFile2.is_open())
					{
						debugFile2 << "\n--- Field: " << field << " ---\n";
						debugFile2 << value;
						debugFile2.close();
					}

					try
					{
						jsonObj[field] = nlohmann::json::parse(value);
					}
					catch(...)
					{
						const char* scratchEnv = std::getenv("OTS_SCRATCH");

						// Sanitize field name for use in filename
						std::string safeFieldName = field;
						for(char& c : safeFieldName)
						{
							if(!std::isalnum(c) && c != '_' && c != '-')
							{
								c = '_';
							}
						}

						std::string fullPath =
						    std::string(scratchEnv ? scratchEnv : ".") +
						    "/Logs/failed_json_parse_" + safeFieldName + ".txt";
						std::ofstream debugFile(fullPath, std::ios::out | std::ios::app);

						if(debugFile.is_open())
						{
							//debugFile << "\n--- Failed JSON Parse ---\n";
							//debugFile << "Field: " << field << "\n";
							//debugFile << "Value: " << value << "\n";
							debugFile << value;
							debugFile.close();
						}

						__SS__ << "Failed to parse JSON for subsystem '" << subsystem
						       << "', field '" << field << "'. "
						       << "Value dumped to " << fullPath << __E__;
						__SS_THROW__;
					}
				}
				else
				{
					jsonObj[field] = value;
				}

				/*
				// Parse json objects and lists
				if (firstRealChar != std::string::npos &&
				(value[firstRealChar] == '{' || value[firstRealChar] == '[')) {
					try {
						jsonObj[field] = nlohmann::json::parse(value);
					} catch (...) { // Fallback, store as string
						__SS__ << "Failed to parse JSON for subsystem '" << subsystem << "', field '" << field << "'. "
						       << "Value dumped to " << fullPath << __E__;
						__SS_THROW__;
					}

					//try {
					//    jsonObj[field] = nlohmann::json::parse(value);
					//} catch (...) { // Fallback, store as string
					//    jsonObj[field] = value;
					//}
				} else {
					jsonObj[field] = value;
				}*/
			}
			std::string jsonString = jsonObj.dump();
			/*std::stringstream jsonBlob;
			jsonBlob << "{";

			bool first = true;
			for (auto const& [field, value] : fieldMap)
			{
			if (!first) jsonBlob << ", ";
			// Basic escaping of quotes might be needed if values contain them
			jsonBlob << "\"" << field << "\": \"" << value << "\"";
			first = false;
			}
			jsonBlob << "}";

			// Construct the SQL Command
			std::stringstream query;
			std::string jsonString = jsonBlob.str();*/
			std::string runNumberStr = std::to_string(runNumber);

			std::string sql = std::string("INSERT INTO ") + dbSchema_ +
			                  ".config "
			                  "(run_number, subsystem, settings, version, create_time) "
			                  "VALUES ($1, $2, $3::jsonb, $4, CURRENT_TIMESTAMP);";

			const char* paramValues[4];
			paramValues[0] = runNumberStr.c_str();
			paramValues[1] = subsystem.c_str();
			paramValues[2] = jsonString.c_str();
			paramValues[3] = "1";

			__COUT__ << "DEBUG INSERTING RUN CONDITION:" << __E__;
			__COUT__ << "paramValues[0]: " << paramValues[0] << __E__;
			__COUT__ << "paramValues[1]: " << paramValues[1] << __E__;
			__COUT__ << "paramValues[2]: " << paramValues[2] << __E__;

			res = PQexecParams(runInfoDbConn_,
			                   sql.c_str(),
			                   4,     // number of parameters
			                   NULL,  // param types (let Postgres infer)
			                   paramValues,
			                   NULL,  // param lengths
			                   NULL,  // param formats
			                   0);    // result format (text)

			// Check result status
			// Note: For INSERT, PQresultStatus usually returns PGRES_COMMAND_OK
			// or PGRES_TUPLES_OK if using RETURNING
			if(PQresultStatus(res) != PGRES_COMMAND_OK &&
			   PQresultStatus(res) != PGRES_TUPLES_OK)
			{
				__SS__ << "INSERT INTO 'config' DATABASE TABLE FAILED!!! PQ ERROR: "
				       << __E__ << PQresultErrorMessage(res) << __E__
				       << "Subsystem: " << subsystem << __E__ << "SQL: " << sql << __E__;
				PQclear(res);
				__SS_THROW__;
			}

			PQclear(res);
		}

		// Update conditionID or return success
		conditionID = runNumber;
		return conditionID;

		//extract run condition from runInfoConditions
		// std::string condition =
		//     runInfoConditions.substr(runInfoConditions.find("Configuration := ") +
		//                              sizeof("Configuration := ") - 1);
		// StringMacros::sanitizeForSQL(condition);

		//extract configuration name and version from runInfoConditions
		// std::string runConfiguration =
		//     runInfoConditions.substr(runInfoConditions.find("Configuration := ") +
		//                              sizeof("Configuration := ") - 1);
		// runConfiguration = runConfiguration.substr(0, runConfiguration.find(')'));

		// std::string runConfigurationVersion =
		//     runConfiguration.substr(runConfiguration.find('(') + 1);
		// boost::trim_right(runConfigurationVersion);
		// StringMacros::sanitizeForSQL(runConfigurationVersion);

		// runConfiguration = runConfiguration.substr(0, runConfiguration.find('('));
		// boost::trim_right(runConfiguration);
		// StringMacros::sanitizeForSQL(runConfiguration);

		// //extract context name and version from runInfoConditions
		// std::string runContext = runInfoConditions.substr(
		//     runInfoConditions.find("Context := ") + sizeof("Context := ") - 1);
		// runContext = runContext.substr(0, runContext.find(')'));

		// std::string runContextVersion = runContext.substr(runContext.find('(') + 1);
		// boost::trim_right(runContextVersion);
		// StringMacros::sanitizeForSQL(runContextVersion);

		// runContext = runContext.substr(0, runContext.find('('));
		// boost::trim_right(runContext);
		// StringMacros::sanitizeForSQL(runContext);

		// std::string backbone = runInfoConditions.substr(
		//     runInfoConditions.find("Backbone := ") + sizeof("Backbone := ") - 1);
		// backbone = backbone.substr(0, backbone.find(')'));

		// std::string backboneVersion = backbone.substr(backbone.find('(') + 1);
		// boost::trim_right(backboneVersion);
		// StringMacros::sanitizeForSQL(backboneVersion);

		// backbone = backbone.substr(0, backbone.find('('));
		// boost::trim_right(backbone);
		// StringMacros::sanitizeForSQL(backbone);

		// __COUT__ << "runInfoConditions " << runInfoConditions << __E__;
		// __COUT__ << "Info from parsing dump..." << __E__;
		// __COUT__ << "\tBackbone := " << backbone << " (" << backboneVersion << ")" <<  __E__;
		// __COUT__ << "\tContext := " << runContext << " (" << runContextVersion << ")" <<  __E__;
		// __COUT__ << "\tConfiguration := " << runConfiguration << " (" << runConfigurationVersion << ")" <<  __E__;

		// __COUT__ << "Run Condition before JSON conversion " << condition.c_str() << __E__;

		/*std::string runInfo = "[";
		for(auto& subsystemPair : runConditionMap)
		{
			if(runInfo.size() > 1)
				runInfo += ", ";
			runInfo += "\"" + subsystemPair.first + "\": {";
			size_t fieldCount = 0;
			for(auto& fieldPair : subsystemPair.second)
			{
				runInfo += "\"" + fieldPair.first + "\": \"" +
						   StringMacros::StringMacros::escapeJSONStringEntities(
							   fieldPair.second) +
						   "\"";
				if(fieldCount < subsystemPair.second.size() - 1)
					runInfo += ", ";
				fieldCount++;
			}
			runInfo += "}";
		}
		runInfo += "]";
		StringMacros::sanitizeForSQL(runInfo);
		__COUT__ << "Configuration dump " << __E__ << runInfo.c_str() << __E__;

		// Get detector setup name (from environment variable or use default)
		const char* detectorSetupEnv  = getenv("OTSDAQ_RUNINFO_DETECTOR_SETUP");
		std::string detectorSetupName = detectorSetupEnv ? detectorSetupEnv : "default";
		StringMacros::sanitizeForSQL(detectorSetupName);

		// Consider getActiveStateMachineName as configuration type name
		// Validate getActiveStateMachineName is provided
		if(getActiveStateMachineName().empty())
		{
			__SS__ << "INSERT INTO 'config' DATABASE TABLE FAILED!!! "
				   << "getActiveStateMachineName (StateMachine UID) is required but was "
					  "not provided."
				   << __E__;
			__SS_THROW__;
		}

		// Sanitize getActiveStateMachineName for SQL safety
		std::string sanitizedConfigTypeName = getActiveStateMachineName();
		StringMacros::sanitizeForSQL(sanitizedConfigTypeName);

		// Try INSERT first - let database validate the config_type exists
		std::ostringstream queryStream;
		queryStream << "INSERT INTO " << dbSchema_ << ".config("
					<< "config_data, "
					<< "create_time, "
					<< "type_id, "
					<< "host_name, "
					<< "detector_setup_id) "
					<< "SELECT '" << runInfo << "', "
					<< "CURRENT_TIMESTAMP, "
					<< "ct.id, "
					<< "'" << hostName << "', "
					<< "ds.id "
					<< "FROM " << dbSchema_ << ".config_type ct "
					<< "CROSS JOIN " << dbSchema_ << ".detector_setup ds "
					<< "WHERE ct.name = '" << sanitizedConfigTypeName << "' "
					<< "AND ds.name = '" << detectorSetupName << "' "
					<< "RETURNING id;";

		std::string query = queryStream.str();
		res               = PQexec(runInfoDbConn_, query.c_str());

		if(PQresultStatus(res) != PGRES_TUPLES_OK)
		{
			__SS__ << "INSERT INTO 'config' DATABASE TABLE FAILED!!! PQ ERROR: "
				   << PQresultErrorMessage(res) << __E__
				   << "Make sure 'ConfigurationDumpOnConfigureFormat' is set to 'Json "
					  "All' in the FSM configuration."
				   << __E__ << "runInfo:" << __E__ << runInfo.c_str() << __E__;
			PQclear(res);
			__SS_THROW__;
		}

		if(PQntuples(res) == 1)
		{
			conditionID = std::stoul(PQgetvalue(res, 0, 0));
			__COUTV__(conditionID);
		}
		else if(PQntuples(res) == 0)
		{
			// No rows returned - check which lookup failed (config_type or detector_setup)
			PQclear(res);

			// Check if config_type exists
			char configTypeBuffer[1024];
			snprintf(configTypeBuffer,
					 sizeof(configTypeBuffer),
					 "SELECT id FROM %s.config_type WHERE name = '%s';",
					 dbSchema_,
					 sanitizedConfigTypeName.c_str());

			PGresult* configTypeRes = PQexec(runInfoDbConn_, configTypeBuffer);
			bool configTypeFound    = (PQresultStatus(configTypeRes) == PGRES_TUPLES_OK &&
									PQntuples(configTypeRes) == 1);
			PQclear(configTypeRes);

			// Check if detector_setup exists
			char detectorSetupBuffer[1024];
			snprintf(detectorSetupBuffer,
					 sizeof(detectorSetupBuffer),
					 "SELECT id FROM %s.detector_setup WHERE name = '%s';",
					 dbSchema_,
					 detectorSetupName.c_str());

			PGresult* detectorSetupRes = PQexec(runInfoDbConn_, detectorSetupBuffer);
			bool      detectorSetupFound =
				(PQresultStatus(detectorSetupRes) == PGRES_TUPLES_OK &&
				 PQntuples(detectorSetupRes) == 1);
			PQclear(detectorSetupRes);

			__SS__ << "INSERT INTO 'config' DATABASE TABLE FAILED!!! " << __E__;

			// Handle config_type not found
			if(!configTypeFound)
			{
				std::vector<std::string> availableConfigTypes =
					getTableNames("config_type");
				appendNotFoundError(ss,
									getActiveStateMachineName(),
									"config_type",
									"StateMachine UID",
									availableConfigTypes);
			}

			// Handle detector_setup not found
			if(!detectorSetupFound)
			{
				std::vector<std::string> availableDetectorSetups =
					getTableNames("detector_setup");
				appendNotFoundError(
					ss,
					detectorSetupName,
					"detector_setup",
					"detector setup name",
					availableDetectorSetups,
					"(Note: This can be set via the OTSDAQ_RUNINFO_DETECTOR_SETUP "
					"environment variable)");
			}

			// If both are missing, provide a combined message
			if(!configTypeFound && !detectorSetupFound)
			{
				ss << __E__
				   << "Both the StateMachine UID and detector setup name are invalid. "
				   << "Please fix both issues listed above." << __E__;
			}
			else if(!configTypeFound)
			{
				ss << __E__
				   << "Please ensure that the StateMachine UID in your configuration "
				   << "matches one of the config_type names listed above." << __E__;
			}
			else if(!detectorSetupFound)
			{
				ss << __E__
				   << "Please ensure that the detector setup name (from environment "
					  "variable "
				   << "OTSDAQ_RUNINFO_DETECTOR_SETUP or default 'default') "
				   << "matches one of the detector_setup names listed above." << __E__;
			}

			__SS_THROW__;
		}
		else
		{
			__SS__ << "RETRIEVE CONDITION_ID FROM 'config' DATABASE TABLE "
					  "FAILED!!! Unexpected number of rows returned: "
				   << PQntuples(res) << __E__;
			PQclear(res);
			__SS_THROW__;
		}

		std::vector<std::vector<std::string>> subsystemsInfo = {
			{       // "1" 	// config ID
			 "crv"  // subsystem ID
			 ,
			 "{\"data\": \"test\"}",
			 "alias_1",
			 "context_1",
			 "1"  // context group key
			 ,
			 "conf_group_name_1",
			 "1"  // config group key
			 ,
			 "backbone_name_1",
			 "1",
			 "config_db_uir_1",
			 "subsystem_sw_version_id_1"},
			{       // "1" 	// config ID
			 "dcs"  // subsystem ID
			 ,
			 "{\"data\": \"test\"}",
			 "alias_2",
			 "context_2",
			 "1"  // context group key
			 ,
			 "conf_group_name_2",
			 "1"  // config group key
			 ,
			 "backbone_name_2",
			 "1",
			 "config_db_uir_2",
			 "subsystem_sw_version_id_1"},
			{           // "1" 	// config ID
			 "trigger"  // subsystem ID
			 ,
			 "{\"data\": \"test\"}",
			 "alias_3",
			 "context_3",
			 "1"  // context group key
			 ,
			 "conf_group_name_3",
			 "1"  // config group key
			 ,
			 "backbone_name_3",
			 "1",
			 "config_db_uir_3",
			 "subsystem_sw_version_id_1",
			 "3"}};

		// char* artadqPartition = __ENV__("ARTDAQ_PARTITION");

		for(uint8_t i = 0; i < subsystemsInfo.size(); i++)
		{
			char buffer[2048];
			snprintf(buffer,
					 sizeof(buffer),
					 "INSERT INTO %s.config_subsystem_data(						\
											  config_id					\
											, subsystem				\
											, data		\
											, create_time)				\
											  VALUES ('%ld','%s','%s',CURRENT_TIMESTAMP) \
											  RETURNING config_id;",
					 dbSchema_,
					 conditionID,
					 subsystemsInfo[i][0].c_str(),
					 subsystemsInfo[i][1].c_str());

			res = PQexec(runInfoDbConn_, buffer);

			if(PQresultStatus(res) != PGRES_TUPLES_OK)
			{
				__SS__ << "INSERT INTO 'config_subsystem_data' DATABASE TABLE FAILED!!! "
						  "PQ ERROR: "
					   << PQresultErrorMessage(res) << __E__;
				PQclear(res);
				__SS_THROW__;
			}

			PQclear(res);

			snprintf(buffer,
					 sizeof(buffer),
					 "INSERT INTO %s.config_subsystem(				    	\
											  config_id					\
											, subsystem 				\
											, config_alias				\
											, context_name				\
											, context_key				\
											, config_group_name			\
											, config_group_key			\
											, backbone_name 			\
											, backbone_key				\
											, config_db_uri				\
											, sw_version_id	\
											, create_time)			\
											  VALUES ('%ld','%s','%s','%s','%s','%s','%s','%s','%s','%s','%s',CURRENT_TIMESTAMP) \
											  RETURNING config_id;",
					 dbSchema_,
					 conditionID,
					 (subsystemsInfo[i][0]).c_str(),
					 (subsystemsInfo[i][2]).c_str(),
					 (subsystemsInfo[i][3]).c_str(),
					 (subsystemsInfo[i][4]).c_str(),
					 (subsystemsInfo[i][5]).c_str(),
					 (subsystemsInfo[i][6]).c_str(),
					 (subsystemsInfo[i][7]).c_str(),
					 (subsystemsInfo[i][8]).c_str(),
					 (subsystemsInfo[i][9]).c_str(),
					 (subsystemsInfo[i][10]).c_str());

			res = PQexec(runInfoDbConn_, buffer);

			if(PQresultStatus(res) != PGRES_TUPLES_OK)
			{
				__SS__ << "INSERT INTO 'config_subsystem' DATABASE TABLE FAILED!!! PQ "
						  "ERROR: "
					   << PQresultErrorMessage(res) << __E__;
				PQclear(res);
				__SS_THROW__;
			}

			PQclear(res);

		}  // end for loop*/
	}

	if(conditionID == (unsigned int)-1)
	{
		__SS__ << "Impossible condition_id not defined by run info plugin!" << __E__;
		__SS_THROW__;
	}

	return conditionID;  // in this scheme we return the run number since that's used to track the run condition
}  //end insertRunCondition()

//==============================================================================
/// claimNextRunNumber
///		Creates a new run record in the database and claims the next available run number.
///		This is called by the state machine when transitioning from Configured to Running state
///		(Start transition).
///
///		@param configureConditionID - The database ID of the config record (from insertRunCondition).
///		                     This links the run to the configuration that was used.
///		@param comment - User-provided comment/description for the run. This is the log entry
///                      from the state machine transition.
///		@return runNumber - The database-generated run number for the new run record. This is auto-generated by the database and returned via the RETURNING clause. Also inserts a START transition record into the run_transition table.
unsigned int DBRunInfo::claimNextRunNumber(unsigned int       configureConditionID,
                                           const std::string& comment)
{
	//if(configureConditionID == (unsigned int)-1)
	//{
	//	__SS__ << "Impossible condition ID number not retrieved by run info plugin!"
	//	       << __E__;
	//	__SS_THROW__;
	//}

	unsigned int runNumber = (unsigned int)-1;
	__COUT__ << "claiming next Run Number" << __E__;

	int runInfoDbConnStatus_ = checkAndReconnectDb("for insert new run number and info");

	// write run info into db
	if(runInfoDbConn_ && runInfoDbConnStatus_ == 1)
	{
		PGresult* res;

		//extract configuration name and version from runInfoConditions
		// std::string runConfiguration =
		//     runInfoConditions.substr(runInfoConditions.find("Configuration := ") +
		//                              sizeof("Configuration := ") - 1);
		// runConfiguration = runConfiguration.substr(0, runConfiguration.find(')'));

		// std::string runConfigurationVersion =
		//     runConfiguration.substr(runConfiguration.find('(') + 1);
		// boost::trim_right(runConfigurationVersion);
		// StringMacros::sanitizeForSQL(runConfigurationVersion);

		// runConfiguration = runConfiguration.substr(0, runConfiguration.find('('));
		// boost::trim_right(runConfiguration);
		// StringMacros::sanitizeForSQL(runConfiguration);

		// //extract context name and version from runInfoConditions
		// std::string runContext = runInfoConditions.substr(
		//     runInfoConditions.find("Context := ") + sizeof("Context := ") - 1);
		// runContext = runContext.substr(0, runContext.find(')'));

		// std::string runContextVersion = runContext.substr(runContext.find('(') + 1);
		// boost::trim_right(runContextVersion);
		// StringMacros::sanitizeForSQL(runContextVersion);

		// runContext = runContext.substr(0, runContext.find('('));
		// boost::trim_right(runContext);
		// StringMacros::sanitizeForSQL(runContext);

		//insert a new row in the runs table
		// __COUT__ << "Insert new run info in the runs database table, run "
		//             "configuration is: "
		//          << runConfiguration << " , run context is: " << runContext << __E__;

		// NOTES :
		// "production" -- runs
		// "tests" -- runs

		// runs renamed to "runs"
		//    runs has less columns (moved to different table)
		// run_condition renamed global_config

		// Sanitize comment for SQL safety
		std::string sanitizedComment = comment;
		StringMacros::sanitizeForSQL(sanitizedComment);

		// Get both runAlias and name from ActiveStateMachine
		std::string runAlias = StringMacros::convertEnvironmentVariables(
		    "${OTS.ActiveStateMachine.runAlias}");
		StringMacros::sanitizeForSQL(runAlias);

		std::string stateMachineName = getActiveStateMachineName();
		StringMacros::sanitizeForSQL(stateMachineName);

		__COUTV__(runAlias);
		__COUTV__(stateMachineName);

		// Query run_type table to get the type_id for this run type (using runAlias)
		std::ostringstream typeQueryStream;
		typeQueryStream << "SELECT id FROM " << dbSchema_ << ".run_type "
		                << "WHERE name = '" << runAlias << "';";

		std::string typeQuery = typeQueryStream.str();
		PGresult*   typeRes   = PQexec(runInfoDbConn_, typeQuery.c_str());

		if(PQresultStatus(typeRes) != PGRES_TUPLES_OK)
		{
			__SS__ << "QUERY run_type TABLE FAILED!!! PQ ERROR: "
			       << PQresultErrorMessage(typeRes) << __E__;
			PQclear(typeRes);
			__SS_THROW__;
		}

		int runTypeId = -1;
		if(PQntuples(typeRes) == 1)
		{
			runTypeId = atoi(PQgetvalue(typeRes, 0, 0));
		}
		else
		{
			// run_type not found, get list of valid types for error message
			std::ostringstream validTypesQueryStream;
			validTypesQueryStream << "SELECT name FROM " << dbSchema_ << ".run_type "
			                      << "ORDER BY id;";

			std::string validTypesQuery = validTypesQueryStream.str();
			PGresult*   validTypesRes   = PQexec(runInfoDbConn_, validTypesQuery.c_str());

			__SS__ << "Unknown run_type '" << runAlias << "'! "
			       << "(StateMachine name: " << stateMachineName << ") "
			       << "Valid run types are: ";

			if(PQresultStatus(validTypesRes) == PGRES_TUPLES_OK &&
			   PQntuples(validTypesRes) > 0)
			{
				for(int i = 0; i < PQntuples(validTypesRes); ++i)
				{
					if(i > 0)
						ss << ", ";
					ss << PQgetvalue(validTypesRes, i, 0);
				}
			}
			else
			{
				ss << "(none defined in database)";
			}

			ss << __E__;
			PQclear(validTypesRes);
			PQclear(typeRes);
			__SS_THROW__;
		}
		PQclear(typeRes);

		// Build INSERT query using std::ostringstream to avoid buffer overflow
		std::ostringstream queryStream;
		queryStream << "INSERT INTO " << dbSchema_ << ".run ("
		            << "  comment, "
		            << "  run_type_id, "
		            << "  create_time) "
		            << " VALUES ("
		            << "  '" << sanitizedComment << "', "
		            << "  " << runTypeId << ", "
		            << "  CURRENT_TIMESTAMP) "
		            << " RETURNING run_number;";

		std::string query = queryStream.str();
		res               = PQexec(runInfoDbConn_, query.c_str());

		if(PQresultStatus(res) != PGRES_TUPLES_OK)
		{
			__SS__ << "INSERT INTO 'run' DATABASE TABLE FAILED!!! PQ ERROR: "
			       << PQresultErrorMessage(res) << __E__;
			PQclear(res);
			__SS_THROW__;
		}

		if(PQntuples(res) == 1)
		{
			runNumber = atoi(PQgetvalue(res, 0, 0));
			__COUTV__(runNumber);
		}
		else
		{
			__SS__ << "RETRIEVE RUN NUMBER FROM 'run' DATABASE TABLE "
			          "FAILED!!! PQ ERROR: "
			       << PQresultErrorMessage(res) << __E__;
			PQclear(res);
			__SS_THROW__;
		}

		PQclear(res);

		// write run start transition into run_transition table
		updateRunInfo(runNumber, RunInfoVInterface::RunTransitionType::START, comment);
	}

	// __SS__ << "Halting..." << __E__;
	// __SS_THROW__;

	//=========================================================================
	// READ DB
	//=========================================================================

	// PGresult* readRes;
	// char      readBuffer[1024];
	// // SELECT *
	// // FROM runs
	// // ORDER BY run_id ASC
	// // LIMIT 1;

	// snprintf(readBuffer,
	//          sizeof(readBuffer),
	//          "SELECT * FROM %s.runs(
	// 		ORDER BY run_id ASC
	// 		LIMIT;",
	//          dbSchema_);

	// readRes = PQexec(runInfoDbConn_, readBuffer);

	// if(PQresultStatus(readRes) != PGRES_TUPLES_OK)
	// {
	// 	__SS__
	// 	    << "INSERT INTO 'runs' DATABASE TABLE FAILED!!! PQ ERROR: "
	// 	    << PQresultErrorMessage(res) << __E__;
	// 	PQclear(res);
	// 	__SS_THROW__;
	// }

	// if(PQntuples(readRes) == 1)
	// {
	// 	runNumber = atoi(PQgetvalue(readRes, 0, 0));
	// 	__COUTV__(runNumber);
	// }
	// else
	// {
	// 	__SS__ << "RETRIEVE RUN NUMBER FROM 'runs' DATABASE TABLE "
	// 	          "FAILED!!! PQ ERROR: "
	// 	       << PQresultErrorMessage(readRes) << __E__;
	// 	PQclear(res);
	// 	__SS_THROW__;
	// }

	// PQclear(readRes);

	// __SS__ << "Halting..." << __E__;
	// __SS_THROW__;

	if(runNumber == (unsigned int)-1)
	{
		__SS__ << "Impossible run number not defined by run info plugin!" << __E__;
		__SS_THROW__;
	}

	return runNumber;
}  //end claimNextRunNumber()

//==============================================================================
TransitionTypeInfo DBRunInfo::getTransitionTypeInfo(
    RunInfoVInterface::RunTransitionType runStopType)
{
	// Map RunTransitionType enum to database transition type ID and description
	switch(runStopType)
	{
	case RunInfoVInterface::RunTransitionType::HALT:
		return {0, "Running to Halt - Abort"};
	case RunInfoVInterface::RunTransitionType::STOP:
		return {1, "Running to Configure - Stop"};
	case RunInfoVInterface::RunTransitionType::ERROR:
		return {2, "Other state to Error - Error"};
	case RunInfoVInterface::RunTransitionType::PAUSE:
		return {3, "Running to Pause - Pause"};
	case RunInfoVInterface::RunTransitionType::RESUME:
		return {4, "Pause to Running - Resume"};
	case RunInfoVInterface::RunTransitionType::START:
		return {5, "Configure to Running - Start"};
	case RunInfoVInterface::RunTransitionType::STOP_COMPLETE:
		return {6, "Stop transition completed successfully"};
	case RunInfoVInterface::RunTransitionType::HALT_COMPLETE:
		return {7, "Halt transition completed successfully"};
	default:
		__SS__ << "Unknown RunTransitionType: " << static_cast<int>(runStopType) << __E__;
		__SS_THROW__;
	}
}  //end getTransitionTypeInfo()

//==============================================================================
/// updateRunInfo
///		Inserts a transition record into the database for a specific run.
///		This is called by the state machine during various transitions to track
///		when state changes occur for a run (e.g., Start, Stop, Pause, Resume, Halt, Error).
///      Only states Configured and "above" are recorded (aka relevant for run)
///
///		@param runConditionID - For Mu2e, the runConditionID is the database run number for which to record the transition.
///		                   This should be a valid run number that was previously created
///		                   and value returned via claimNextRunNumber().
///		@param runTransitionType - The type of transition being recorded. Valid values are:
///		                     - HALT: Running/Paused to Halt (Abort)
///		                     - STOP: Running to Configure (Stop)
///		                     - ERROR: Any state to Error
///		                     - PAUSE: Running to Pause
///		                     - RESUME: Pause to Running
///		                     - START: Configure to Running (typically called from claimNextRunNumber)
///		@param comment - User-provided comment/description for the transition. This is the log entry
///                      from the state machine transition.
///		@return void - Inserts a record into the run_transition table with the run_number,
///		               transition type_id (mapped from runStopType), and current timestamp.
void DBRunInfo::updateRunInfo(unsigned int       runConditionID,
                              RunTransitionType  runTransitionType,
                              const std::string& comment,
                              const std::string& metadata)
{
	// For Mu2e, the runConditionID is the run number (for now!)
	unsigned int runNumber = runConditionID;
	__COUT__ << "Updating run transition for run number " << runNumber << __E__;

	int runInfoDbConnStatus_ = checkAndReconnectDb("to update the transition");

	// Insert the transition and time into db
	if(runInfoDbConn_ && runInfoDbConnStatus_ == 1)
	{
		// Get transition type information from mapping
		TransitionTypeInfo transitionInfo = getTransitionTypeInfo(runTransitionType);

		std::string transitionDescription = "'" + transitionInfo.description + "'";
		StringMacros::sanitizeForSQL(
		    transitionDescription);  //in case transitionDescription is used instead of int

		PGresult* res;

		// Build INSERT query using std::ostringstream to avoid buffer overflow
		std::ostringstream queryStream;
		queryStream << "INSERT INTO " << dbSchema_ << ".run_transition("
		            << "run_number, "
		            << "type_id, "
		            << "transition_time) "
		            << "VALUES (" << boost::numeric_cast<long int>(runNumber) << ","
		            << boost::numeric_cast<int>(transitionInfo.typeId)
		            << ",CURRENT_TIMESTAMP);";

		std::string query = queryStream.str();
		res               = PQexec(runInfoDbConn_, query.c_str());

		if(PQresultStatus(res) != PGRES_COMMAND_OK)
		{
			__SS__ << "INSERT " << transitionDescription
			       << " TRANSITION INTO DATABASE TABLE FAILED!!! PQ ERROR: "
			       << PQresultErrorMessage(res) << __E__;
			PQclear(res);
			__SS_THROW__;
		}
		PQclear(res);

		__COUT__ << "Insert: " << transitionDescription
		         << " transition into the run_transition Database table" << __E__;

		// If this is a STOP transition, also record the end comment
		if(transitionInfo.typeId == 1)
		{
			// Use PQescapeLiteral to properly escape the comment (handles reserved keywords like 'end')
			char* escapedComment =
			    PQescapeLiteral(runInfoDbConn_, comment.c_str(), comment.length());
			if(!escapedComment)
			{
				__SS__ << "FAILED TO ESCAPE COMMENT FOR DATABASE!!! PQ ERROR: "
				       << PQerrorMessage(runInfoDbConn_) << __E__;
				__SS_THROW__;
			}

			bool discardRun = false;
			if(!metadata.empty())
			{
				try
				{
					auto j = nlohmann::json::parse(metadata);
					if(j.contains("discardRun"))
						discardRun = j["discardRun"].get<bool>();
				}
				catch(const nlohmann::json::exception& e)
				{
					__COUT_WARN__ << "Failed to parse metadata JSON: " << e.what()
					              << __E__;
				}
			}

			std::ostringstream endCommentQueryStream;
			endCommentQueryStream
			    << "INSERT INTO " << dbSchema_ << ".run_end_info("
			    << "run_number, "
			    << "comment, "
			    << "discard_run, "
			    << "create_time) "
			    << "VALUES (" << boost::numeric_cast<long int>(runNumber) << ","
			    << escapedComment << "," << (discardRun ? "TRUE" : "NULL")
			    << ",CURRENT_TIMESTAMP);";

			std::string endCommentQuery = endCommentQueryStream.str();
			PGresult*   endCommentRes   = PQexec(runInfoDbConn_, endCommentQuery.c_str());

			PQfreemem(escapedComment);  // Free the escaped string

			if(PQresultStatus(endCommentRes) != PGRES_COMMAND_OK)
			{
				__SS__ << "INSERT END COMMENT INTO DATABASE TABLE FAILED!!! PQ ERROR: "
				       << PQresultErrorMessage(endCommentRes) << __E__;
				PQclear(endCommentRes);
				__SS_THROW__;
			}
			PQclear(endCommentRes);

			__COUT__ << "Insert: End comment into the run_end_info Database table"
			         << __E__;
		}
	}

	if(runNumber == (unsigned int)-1)
	{
		__SS__ << "Impossible run number not defined by run info plugin!" << __E__;
		__SS_THROW__;
	}

	__COUT__ << "done with the run_info database for updating the transition!" << __E__;
}  //end updateRunInfo()

//==============================================================================
std::vector<std::vector<std::string>> DBRunInfo::getRunRecords(
    unsigned int       startTime,
    unsigned int       endTime,
    const std::string& queryFilter,
    const std::string& runType)
{
	__COUT__ << "getRunRecords() reached" << __E__;
	std::vector<std::vector<std::string>> runRecords;

	int runInfoDbConnStatus_ = checkAndReconnectDb("to select run records");

	// select run info from db
	if(runInfoDbConn_ && runInfoDbConnStatus_ == 1)
	{
		PGresult*          res;
		std::string        filterClause;
		std::ostringstream queryStream;

		if(!queryFilter.empty())
			filterClause = " " + queryFilter;

		std::string runTypeClause;
		if(!runType.empty())
		{
			std::string runTypePattern = "%" + runType + "%";
			char*       escapedRunType = PQescapeLiteral(
                runInfoDbConn_, runTypePattern.c_str(), runTypePattern.length());
			if(!escapedRunType)
			{
				__SS__ << "Failed to escape runType filter for getRunRecords()." << __E__;
				__SS_THROW__;
			}

			runTypeClause = " AND run_type_name LIKE ";
			runTypeClause += escapedRunType;
			PQfreemem(escapedRunType);
		}

		queryStream << "SELECT run_number"
		            << ", start_time as run_time"
		            << ", run_type_name as run_type"
		            << ", artdaq_partition"
		            << ", host_name"
		            << ", config_alias as config_id"
		            << ", comment as shifter_comment"
		            << ", start_time"
		            << ", stop_time"
		            << " FROM " << dbSchema_ << ".view_run_summary"
		            << " WHERE run_status = 'completed'"
		            << " AND start_time BETWEEN TO_TIMESTAMP("
		            << boost::numeric_cast<int>(startTime) << ") AND TO_TIMESTAMP("
		            << boost::numeric_cast<int>(endTime) << ")" << runTypeClause
		            << filterClause << " ORDER BY run_number DESC;";

		std::string query = queryStream.str();
		res               = PQexec(runInfoDbConn_, query.c_str());

		if(PQresultStatus(res) != PGRES_TUPLES_OK)
		{
			__SS__ << "getRunRecords() SELECT FROM 'view_run_summary' DATABASE TABLE "
			          "FAILED!!! PQ ERROR: "
			       << PQresultErrorMessage(res) << __E__;
			PQclear(res);
			__SS_THROW__;
		}

		__COUT__ << "PQntuples(res) " << PQntuples(res) << "Query: " << query << __E__;
		runRecords = convertResultToVector(res);
		if(!runRecords.empty())
		{
			__COUT__ << "Run records retrieved" << __E__;
		}
		else
		{
			// __SS__ << "getRunRecords() RETRIEVE RUN RECORDS FROM 'runs' DATABASE TABLE "
			//           "FAILED!!! PQ ERROR: "
			//        << PQresultErrorMessage(res) << __E__;
			// PQclear(res);
			// __SS_THROW__;
		}

		PQclear(res);
	}

	return runRecords;
}  //end getRunRecords()

//==============================================================================
/*
std::vector<std::vector<std::string>> DBRunInfo::getRunConfigSubsystemInfo(
    uint64_t configID)
{
	std::vector<std::vector<std::string>> configRecords;
	PGresult*                             res;
	char                                  buffer[2048];

	__COUT__ << "configID " << configID << __E__;

	snprintf(buffer,
	         sizeof(buffer),
	         "SELECT cs.config_id, cs.subsystem, csd.data as subsystem_config_data, "
	         "cs.config_alias, cs.context_name, cs.context_key, cs.config_group_name, "
	         "cs.config_group_key, cs.backbone_name, cs.backbone_key, cs.config_db_uri, "
	         "cs.sw_version_id, cs.create_time "
	         "FROM %s.config_subsystem cs "
	         "LEFT JOIN %s.config_subsystem_data csd "
	         "  ON cs.config_id = csd.config_id AND cs.subsystem = csd.subsystem "
	         "WHERE cs.config_id = %ld "
	         "ORDER BY cs.subsystem;",
	         dbSchema_,
	         dbSchema_,
	         configID);

	res = PQexec(runInfoDbConn_, buffer);

	if(PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		__SS__ << "getRunConfigSubsystemInfo() SELECT FROM 'subsystem_config' DATABASE "
		          "TABLE "
		          "FAILED!!! PQ ERROR: "
		       << PQresultErrorMessage(res) << __E__;
		PQclear(res);
		__SS_THROW__;
	}

	__COUT__ << "PQntuples(res) " << PQntuples(res) << "Query: " << buffer << __E__;

	configRecords = convertResultToVector(res);
	if(!configRecords.empty())
	{
		__COUT__ << "Subsystem config retrieved" << __E__;
	}

	PQclear(res);
	return configRecords;
}  //end getRunConfigSubsystemInfo()
*/

//==============================================================================
// TODO: change function name to config ID
/*
std::vector<std::vector<std::string>> DBRunInfo::getRunConditionByID(uint64_t conditionID)
{
	__COUT__ << "getRunConditionByID() reached" << __E__;
	std::vector<std::vector<std::string>> conditionRecords;

	int runInfoDbConnStatus_ = checkAndReconnectDb("to select run condition record");

	// select run info from db
	if(runInfoDbConn_ && runInfoDbConnStatus_ == 1)
	{
		PGresult* res;
		char      buffer[1024];

		snprintf(buffer,
		         sizeof(buffer),
		         "SELECT config_data"
		         ", create_time"
		         " FROM %s.config"
		         " WHERE id = %ld;",
		         dbSchema_,
		         conditionID);

		res = PQexec(runInfoDbConn_, buffer);

		if(PQresultStatus(res) != PGRES_TUPLES_OK)
		{
			__SS__ << "getRunRecords() SELECT FROM 'run_condition' DATABASE TABLE "
			          "FAILED!!! PQ ERROR: "
			       << PQresultErrorMessage(res) << __E__;
			PQclear(res);
			__SS_THROW__;
		}

		__COUT__ << "PQntuples(res) " << PQntuples(res) << __E__;
		conditionRecords = convertResultToVector(res);
		if(conditionRecords.empty())
		{
			__SS__ << "getRunConditionByID() RETRIEVE RUN CONDITION RECORD FROM "
			          "'run_condition' DATABASE TABLE "
			          "FAILED!!! No records found."
		       << __E__;
			PQclear(res);
			__SS_THROW__;
		}
		__COUT__ << "Run condition record retrieved" << __E__;

		PQclear(res);
	}

	return conditionRecords;
}  //end getRunConditionByID()
*/

DEFINE_OTS_PROCESSOR(DBRunInfo)
