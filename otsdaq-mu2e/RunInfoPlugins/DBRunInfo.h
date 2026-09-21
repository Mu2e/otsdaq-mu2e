#ifndef _ots_DBRunInfo_h_
#define _ots_DBRunInfo_h_

#include <libpq-fe.h> /* for PGconn */
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include "otsdaq/FiniteStateMachine/RunInfoVInterface.h"  // for Run Info plugins
#include "otsdaq/TableCore/TableView.h"

namespace ots
{

// Structure to hold transition type mapping information
struct TransitionTypeInfo
{
	int         typeId;
	std::string description;
};

/// ~~ DBRunInfo expected flow (managed by GatewaySupervisor) ~~:
///
///		Configure transition:
///			- insertConfigureCondition(<local configure blob>, <comment>)
///				==> Run Info plugin records configure conditions associated with this Configure transition.
///					The blob is local to the parent GatewaySupervisor (i.e. not from subsystems, as subsystems could configure asynchronously)
///
///		Pre-start transition:
///			- claimNextRunNumber(<configure condition ID>, <comment>)
///				==> Run Info plugin retrieves/returns next run number from run number
///
///		During start transition:
///			- Gateway Supervisor collects configure blobs from all subsystem Gateways
///				==> Run Info plugin not involved
///
///		End of start transition:
/// 		- insertRunCondition(<run number>, <map of blob from all subsystems>, <configure condition ID>, <comment>)
///				==> Run Info plugin records run conditions as desired associated with run number)
///
///		In stop/pause/resume/halt/error transitions:
/// 		- updateRunInfo(<run number>, <transition type>, <comment>)
///				==> Run Info plugin records run transition as desired associated with run number)
///
class DBRunInfo : public RunInfoVInterface
{
  public:
	DBRunInfo(const std::string& runInfoPluginClassName,
	          const std::string& activeStateMachineName);
	virtual ~DBRunInfo(void);

	virtual unsigned int insertConfigureCondition(const std::string& /*blob*/,
	                                              const std::string& /*comment*/)
	{
		__COUT__ << "Do nothing for insertConfigureCondition()" << __E__;
		return -1;
	};
	virtual unsigned int claimNextRunNumber(unsigned int /* configureConditionID */,
	                                        const std::string& /* comment */);
	virtual unsigned int insertRunCondition(
	    unsigned int /* runNumber */,
	    const std::map<
	        std::string /* subsystem */,
	        std::map<std::string /*type/name/field */, std::string /* value */>>&
	    /* runConditionMap */,
	    unsigned int /* configureConditionID */,
	    const std::string& /* comment */);

	virtual void updateRunInfo(unsigned int /* runConditionID */,
	                           RunTransitionType /* runTransitionType */,
	                           const std::string& /* comment */,
	                           const std::string& /* metadata */ = "");

	/// Get functions ----

	///start queryFilter with 'AND' to fiter more the selection
	virtual std::vector<std::vector<std::string>> getRunRecords(
	    unsigned int /* startTime */,
	    unsigned int /* endTime */,
	    const std::string& queryFilter = "",
	    const std::string& runType     = "");

	virtual std::vector<std::vector<std::string>> getRunConditionByID(
	    uint64_t /* conditionID*/)
	{
		__SS__ << "getRunConditionByID() Not implemented by DBRunInfo." << __E__;
		__SS_THROW__;
	};

	virtual std::vector<std::vector<std::string>> getRunConfigSubsystemInfo(
	    uint64_t /* configID */)
	{
		__SS__ << "getRunConfigSubsystemInfo() Not implemented by DBRunInfo." << __E__;
		__SS_THROW__;
	};

  private:
	const char* dbname_;
	const char* dbhost_;
	const char* dbport_;
	const char* dbuser_;
	const char* dbpwd_;
	const char* dbSchema_;
	PGconn*     runInfoDbConn_ = nullptr;

	void openDbConnection(void);

	/// Helper functions for error reporting
	std::vector<std::string> getTableNames(const std::string& tableName);
	void                     appendNotFoundError(std::stringstream&              ss,
	                                             const std::string&              providedName,
	                                             const std::string&              tableName,
	                                             const std::string&              entityDescription,
	                                             const std::vector<std::string>& availableNames,
	                                             const std::string&              additionalNote = "");

	/// Helper function to get transition type information
	static TransitionTypeInfo getTransitionTypeInfo(
	    RunInfoVInterface::RunTransitionType runTransitionType);

	/// Helper function to check and reconnect database connection if needed
	int checkAndReconnectDb(const std::string& operationDescription);

	/// Helper function to convert PGresult to vector<vector<string>>
	std::vector<std::vector<std::string>> convertResultToVector(PGresult* res);
};
}  // namespace ots

#endif
