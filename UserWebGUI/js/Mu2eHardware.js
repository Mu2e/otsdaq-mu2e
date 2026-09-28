// Mu2eHardware.js — Reusable library for Mu2e hardware queries and macro execution.
// Any OTSDAQ page can include this to access FE structure, macro lists, and macro execution.
// Requires: Globals.js, Debug.js, DesktopContent.js, ConfigurationAPI.js loaded first.

var Mu2eHardware = Mu2eHardware || {};

(function () {
	"use strict";

	// =========================================================================
	// Internal state
	// =========================================================================

	var _feClassToFEsMap = {};   // feClass -> [uid, uid, ...]
	var _feToMacrosMap = {};     // uid -> { macroName -> { inputs, outputs, feClass, supervisor, lid, ... } }
	var _feStructure = null;     // parsed JSON from DTCInterfaceTable structure
	var _configGuiLid = 0;       // discovered at runtime via getAppId
	var _macroMakerLid = 0;      // captured on first use from the page's original LID

	// =========================================================================
	// Public accessors
	// =========================================================================

	Mu2eHardware.getFeClassToFEsMap = function () { return _feClassToFEsMap; };
	Mu2eHardware.getFeToMacrosMap = function () { return _feToMacrosMap; };
	Mu2eHardware.getStructure = function () { return _feStructure; };

	Mu2eHardware.getMacrosForDevice = function (uid) {
		return _feToMacrosMap[uid] || null;
	};

	Mu2eHardware.getDevicesByClass = function (feClass) {
		return _feClassToFEsMap[feClass] || [];
	};

	Mu2eHardware.getAllClasses = function () {
		return Object.keys(_feClassToFEsMap);
	};

	// =========================================================================
	// fetchActiveConfig — get active configuration group info
	//   callback({context: {groupName, groupKey}, config: {groupName, groupKey},
	//             aliases: [{alias, name, key, groupType}, ...]})
	// =========================================================================

	Mu2eHardware.fetchActiveConfig = function (callback) {
		var doFetch = function () {
			_setConfigGuiLid();
			ConfigurationAPI.getAliasesAndGroups(function (retObj) {
			var result = {};

			// Active groups
			if (retObj && retObj.activeGroups) {
				result.context = retObj.activeGroups.Context || {};
				result.config = retObj.activeGroups.Configuration || {};
			}

			// Find which alias matches the active groups
			result.contextAlias = "";
			result.configAlias = "";
			if (retObj && retObj.aliases) {
				var ctxAliases = retObj.aliases.Context || [];
				for (var i = 0; i < ctxAliases.length; ++i) {
					if (ctxAliases[i].name === result.context.groupName &&
						ctxAliases[i].key === result.context.groupKey) {
						result.contextAlias = ctxAliases[i].alias;
						break;
					}
				}
				var cfgAliases = retObj.aliases.Configuration || [];
				for (var i = 0; i < cfgAliases.length; ++i) {
					if (cfgAliases[i].name === result.config.groupName &&
						cfgAliases[i].key === result.config.groupKey) {
						result.configAlias = cfgAliases[i].alias;
						break;
					}
				}
			}

			if (callback) callback(result);
		}, false, false);
		};

		if (_configGuiLid) {
			doFetch();
		} else {
			_discoverConfigGuiLid(doFetch);
		}
	};

	// =========================================================================
	// fetchStructure — get DTC/ROC hierarchy from DTCInterfaceTable
	// =========================================================================

	Mu2eHardware.fetchStructure = function (callback) {
		Debug.log("Mu2eHardware.fetchStructure()");

		var doFetch = function () {
			_setConfigGuiLid();

			ConfigurationAPI.getStructureStatus("DTCInterfaceTable",
				function (json) {

					if (!json) {
						Debug.log("Mu2eHardware: No structure data returned.", Debug.HIGH_PRIORITY);
						_feStructure = null;
						if (callback) callback(null);
						return;
					}
					try {
						_feStructure = JSON.parse(json);
					} catch (e) {
						Debug.log("Mu2eHardware: Error parsing structure JSON: " + e, Debug.HIGH_PRIORITY);
						_feStructure = null;
					}
					Debug.log("Mu2eHardware: structure loaded", _feStructure);
					if (callback) callback(_feStructure);
				});
		};

		if (_configGuiLid) {
			doFetch();
		} else {
			_discoverConfigGuiLid(doFetch);
		}
	};

	// =========================================================================
	// fetchMacroList — get all FE macros from MacroMakerSupervisor
	// =========================================================================

	Mu2eHardware.fetchMacroList = function (callback) {
		Debug.log("Mu2eHardware.fetchMacroList()");

		var doFetch = function () {
			_setMacroMakerLid();
			DesktopContent.XMLHttpRequest("Request?RequestType=getFEMacroList", "",
			function (req) {
				_feClassToFEsMap = {};
				_feToMacrosMap = {};

				if (!req || !req.responseXML) {
					Debug.log("Mu2eHardware: No macro list response.", Debug.HIGH_PRIORITY);
					if (callback) callback(_feClassToFEsMap, _feToMacrosMap);
					return;
				}

				var HEADER_FIELDS = 4;
				var feMacros = req.responseXML.getElementsByTagName("FEMacros");

				for (var i = 0; i < feMacros.length; ++i) {
					var macroVar = feMacros[i].getAttribute("value");
					macroVar = macroVar.split(";");

					var supervisor = macroVar[0];
					var lid = macroVar[1];
					var feClass = macroVar[2];
					var feUID = macroVar[3];

					if (!_feClassToFEsMap[feClass])
						_feClassToFEsMap[feClass] = [];
					_feClassToFEsMap[feClass].push(feUID);

					var c = HEADER_FIELDS;
					while (c < macroVar.length) {
						var macroName = macroVar[c];
						if (!_feToMacrosMap[feUID])
							_feToMacrosMap[feUID] = {};

						_feToMacrosMap[feUID][macroName] = {
							"requiredPermissions": macroVar[c + 1],
							"tooltip": decodeURIComponent(macroVar[c + 2]),
							"inputs": [],
							"outputs": [],
							"supervisor": supervisor,
							"lid": lid,
							"feClass": feClass,
						};
						c += 3;
						for (var j = 0; j < (macroVar[c] | 0); ++j)
							_feToMacrosMap[feUID][macroName].inputs.push(
								decodeURIComponent(macroVar[c + 1 + j]));
						c += 1 + (macroVar[c] | 0);
						for (var j = 0; j < (macroVar[c] | 0); ++j)
							_feToMacrosMap[feUID][macroName].outputs.push(
								decodeURIComponent(macroVar[c + 1 + j]));
						c += 1 + (macroVar[c] | 0);
					}
				}

				Debug.log("Mu2eHardware: feClassToFEsMap", _feClassToFEsMap);
				Debug.log("Mu2eHardware: feToMacrosMap", _feToMacrosMap);

				if (callback) callback(_feClassToFEsMap, _feToMacrosMap);
			},
			0, 0, true, true);
		};

		if (_macroMakerLid) {
			doFetch();
		} else {
			_discoverMacroMakerLid(doFetch);
		}
	};

	// =========================================================================
	// runMacro — execute a macro on a single device
	//
	//   uid:       target FE UID
	//   macroName: name of the macro
	//   inputs:    { argName: value, ... }  (or null if none)
	//   callback:  function(result) where result is:
	//              { error, targets: [{ uid, hostname, context, supervisor,
	//                                   execTime, outputs: {name: value} }] }
	//   onProgress: optional function(progressInfo) for async polling updates
	// =========================================================================

	Mu2eHardware.runMacro = function (uid, macroName, inputs, callback, onProgress) {
		var macroObj = _feToMacrosMap[uid] && _feToMacrosMap[uid][macroName];
		if (!macroObj) {
			Debug.log("Mu2eHardware.runMacro: macro '" + macroName + "' not found for " + uid,
				Debug.HIGH_PRIORITY);
			if (callback) callback({ error: "Macro not found" });
			return;
		}

		// An input the page did not fill is sent as "Default": that is the only
		// value the FE side accepts as "use the macro's default" (see
		// getFEMacroConstArgumentValue in FEVInterface.icc); an empty string
		// fails with "is not a number". Same convention as FEMacroTest.html.
		var postData = "inputArgs=";
		var inputArr = macroObj.inputs;
		for (var i = 0; i < inputArr.length; ++i) {
			if (i) postData += ";";
			var val = (inputs && inputs[inputArr[i]] !== undefined &&
				String(inputs[inputArr[i]]).trim() !== "") ? inputs[inputArr[i]] : "Default";
			postData += encodeURIComponent(inputArr[i]) + "," + encodeURIComponent(val);
		}

		postData += "&outputArgs=";
		var outputArr = macroObj.outputs;
		for (var i = 0; i < outputArr.length; ++i) {
			if (i) postData += ",";
			postData += encodeURIComponent(outputArr[i]);
		}

		var requestUrl = "Request?RequestType=runFEMacro" +
			"&feClassSelected=" + macroObj.feClass +
			"&feUIDSelected=" + uid +
			"&macroType=fe" +
			"&macroName=" + macroName +
			"&saveOutputs=0";

		function handleResponse(req, reqParam, errStr) {
			if (errStr) {
				if (callback) callback({ error: errStr });
				return;
			}

			var err = DesktopContent.getXMLValue(req, "Error");
			if (err) {
				if (callback) callback({ error: err });
				return;
			}

			// Async macro — poll for completion
			var notDoneID = DesktopContent.getXMLValue(req, "NotDoneID");
			if (notDoneID) {
				var progressInfo = [];
				var progress = DesktopContent.getXMLChildren(req, "feMacroProgress");
				if (progress && progress.length) {
					for (var p = 0; p < progress.length; ++p) {
						progressInfo.push({
							uid: progress[p].getAttribute("value"),
							percent: DesktopContent.getXMLValue(progress[p], "progress") || 0,
						});
					}
				}
				if (onProgress) onProgress(progressInfo);

				window.setTimeout(function () {
					_setMacroMakerLid();
					DesktopContent.XMLHttpRequest(
						"Request?RequestType=runFEMacro&NotDoneID=" + notDoneID,
						"", handleResponse,
						0, 0, true, true);
				}, 3000);
				return;
			}

			// Parse completed results
			var result = { error: null, targets: [] };
			var feExecs = req.responseXML.getElementsByTagName("feMacroExec");

			for (var f = 0; f < feExecs.length; ++f) {
				var target = {
					uid: DesktopContent.getXMLValue(feExecs[f], "fe_uid"),
					hostname: DesktopContent.getXMLValue(feExecs[f], "fe_hostname"),
					context: DesktopContent.getXMLValue(feExecs[f], "fe_context"),
					supervisor: DesktopContent.getXMLValue(feExecs[f], "fe_supervisor"),
					execTime: DesktopContent.getXMLValue(feExecs[f], "exec_time"),
					outputs: {},
				};
				var outNames = feExecs[f].getElementsByTagName("outputArgs_name");
				var outValues = feExecs[f].getElementsByTagName("outputArgs_value");
				for (var i = 0; i < outNames.length; ++i) {
					// names arrive URI-encoded too ("Active%20Ports")
					target.outputs[_decodeURIComponentSafe(outNames[i].getAttribute("value"))] =
						_decodeURIComponentSafe(outValues[i].getAttribute("value"));
				}
				result.targets.push(target);
			}

			// Also check top-level run args (older response format)
			if (feExecs.length === 0) {
				var runArgNames = req.responseXML.getElementsByTagName("feMacroRunArgs_name");
				var runArgValues = req.responseXML.getElementsByTagName("feMacroRunArgs_value");
				if (runArgNames.length) {
					var target = { uid: uid, outputs: {} };
					for (var i = 0; i < runArgNames.length; ++i) {
						target.outputs[_decodeURIComponentSafe(runArgNames[i].getAttribute("value"))] =
							_decodeURIComponentSafe(runArgValues[i].getAttribute("value"));
					}
					result.targets.push(target);
				}
			}

			if (callback) callback(result);
		}

		_setMacroMakerLid();
		DesktopContent.XMLHttpRequest(requestUrl, postData, handleResponse,
			0, 0, true, true);
	};

	// =========================================================================
	// getActiveDevicesByClass — return UIDs of enabled devices of a given class
	// =========================================================================

	Mu2eHardware.getActiveDevicesByClass = function (feClass) {
		var all = _feClassToFEsMap[feClass] || [];
		if (!_feStructure || !_feStructure.apps) return all;

		var active = [];
		for (var i = 0; i < _feStructure.apps.length; ++i) {
			var app = _feStructure.apps[i];
			if (app.enabled !== "1") continue;
			for (var j = 0; j < app.dtcs.length; ++j) {
				if (app.dtcs[j].enabled === "1" &&
					all.indexOf(app.dtcs[j].name) >= 0)
					active.push(app.dtcs[j].name);
			}
		}
		return active.length ? active : all;
	};

	// =========================================================================
	// runMacroOnAll — run a macro on all active devices of a given class
	//
	//   feClass:   e.g. "DTCFrontEndInterface"
	//   macroName: e.g. "DTC Read"
	//   inputs:    { argName: value } — applied to every device
	//   callback:  function(results) — results = [{uid, result}, ...]
	//   onEach:    optional function(uid, result) — called after each device
	// =========================================================================

	Mu2eHardware.runMacroOnAll = function (feClass, macroName, inputs, callback, onEach) {
		var uids = Mu2eHardware.getActiveDevicesByClass(feClass);
		if (!uids.length) {
			if (callback) callback([]);
			return;
		}

		var results = [];
		var idx = 0;

		function runNext() {
			if (idx >= uids.length) {
				if (callback) callback(results);
				return;
			}

			var uid = uids[idx++];
			Mu2eHardware.runMacro(uid, macroName, inputs,
				function (result) {
					results.push({ uid: uid, result: result });
					if (onEach) onEach(uid, result);
					runNext();
				}
			);
		}

		runNext();
	};

	// =========================================================================
	// runMacroOnUIDs — parallel batch execution for an explicit device list
	//
	//   uids:      ["DTC0", "DTC1", ...]
	//   macroName: e.g. "DTC Read"
	//   inputs:    shared { argName: value } object, or function(uid) returning
	//              one per device (return null to skip that device with an error)
	//   callback:  function(results) — results = [{uid, result}, ...], in
	//              completion order
	//   onEach:    optional function(uid, result, doneCount, totalCount)
	//
	//   All requests are dispatched at once; each device gets its own result.
	// =========================================================================

	Mu2eHardware.runMacroOnUIDs = function (uids, macroName, inputs, callback, onEach) {
		var targets = (uids || []).slice();
		if (!targets.length) {
			if (callback) callback([]);
			return;
		}

		var results = [];
		var remaining = targets.length;

		function finish(uid, result) {
			results.push({ uid: uid, result: result });
			if (onEach) onEach(uid, result, results.length, targets.length);
			if (--remaining === 0 && callback) callback(results);
		}

		function runOne(uid) {
			var uidInputs = (typeof inputs === "function") ? inputs(uid) : inputs;
			if (uidInputs === null) {
				finish(uid, { error: "Required batch target input was not found" });
				return;
			}
			Mu2eHardware.runMacro(uid, macroName, uidInputs, function (result) {
				finish(uid, result);
			});
		}

		for (var i = 0; i < targets.length; ++i) runOne(targets[i]);
	};

	// =========================================================================
	// buildROCMacroInputs — fill the implicit arguments of a DTC-wrapped ROC macro
	//
	//   The DTC exposes each child ROC macro as "ROC FEMacro - X" with a leading
	//   "Target ROC or Mask (...)" input. Many ROC macros also take a leading
	//   "port (...)" input. This fills both by name pattern and lets the caller
	//   supply the rest by prefix.
	//
	//   macroObj: entry from getMacrosForDevice(dtcUID)[macroName]
	//   link:     ROC link ID (0-5), or -1 for every ROC on the DTC
	//   port:     port number, -1 for all; undefined leaves any port input unset
	//   extra:    { "name prefix": value } — case-insensitive prefix match on the
	//             remaining input names, longest prefix wins
	//
	//   Returns { argName: value } ready for runMacro(). Inputs that are not
	//   covered are left out so the FE uses its defaults.
	// =========================================================================

	Mu2eHardware.TARGET_ROC_ARG_RE = /target\s+roc|roc.*(?:target|mask)/i;
	Mu2eHardware.PORT_ARG_RE = /^port\b/i;

	Mu2eHardware.buildROCMacroInputs = function (macroObj, link, port, extra) {
		var inputs = {};
		if (!macroObj) return inputs;
		extra = extra || {};
		var prefixes = Object.keys(extra).sort(function (a, b) {
			return b.length - a.length;
		});
		for (var i = 0; i < macroObj.inputs.length; ++i) {
			var name = macroObj.inputs[i];
			if (Mu2eHardware.TARGET_ROC_ARG_RE.test(name)) {
				inputs[name] = String(link);
				continue;
			}
			if (Mu2eHardware.PORT_ARG_RE.test(name) && port !== undefined) {
				inputs[name] = String(port);
				continue;
			}
			var lower = name.toLowerCase();
			for (var p = 0; p < prefixes.length; ++p) {
				if (lower.indexOf(prefixes[p].toLowerCase()) === 0) {
					inputs[name] = String(extra[prefixes[p]]);
					break;
				}
			}
		}
		return inputs;
	};

	// =========================================================================
	// findOutput — case- and whitespace-insensitive output lookup on a result
	//   from runMacro(). Returns the raw value string, or null.
	// =========================================================================

	Mu2eHardware.findOutput = function (result, name) {
		if (!result || result.error || !result.targets) return null;
		var want = String(name).replace(/\s+/g, " ").trim().toLowerCase();
		for (var t = 0; t < result.targets.length; ++t) {
			var outputs = result.targets[t].outputs || {};
			var keys = Object.keys(outputs);
			for (var i = 0; i < keys.length; ++i) {
				var key = keys[i];
				try { key = decodeURIComponent(key); } catch (e) { /* keep raw */ }
				if (key.replace(/\s+/g, " ").trim().toLowerCase() === want)
					return outputs[keys[i]];
			}
		}
		return null;
	};

	// =========================================================================
	// parseNumber — first number in a macro output value
	//   Handles "123", "123 (0x7b)", "0x7b", "-1". Hex wins if present.
	//   Returns null when no number is found.
	// =========================================================================

	Mu2eHardware.parseNumber = function (value) {
		if (value === null || value === undefined) return null;
		var s = String(value);
		var hex = s.match(/0x([0-9a-f]+)/i);
		if (hex) return parseInt(hex[1], 16);
		var dec = s.match(/-?\d+/);
		return dec ? parseInt(dec[0], 10) : null;
	};

	// =========================================================================
	// findFeClassForUID — look up which FE class a UID belongs to
	// =========================================================================

	Mu2eHardware.findFeClassForUID = function (uid) {
		var allClasses = Object.keys(_feClassToFEsMap);
		for (var i = 0; i < allClasses.length; ++i) {
			var uids = _feClassToFEsMap[allClasses[i]];
			for (var j = 0; j < uids.length; ++j) {
				if (uids[j] === uid) return allClasses[i];
			}
		}
		return null;
	};

	// =========================================================================
	// getDeviceType — determine if a UID is a "dtc", "roc", or "other"
	//   based on the structure data
	// =========================================================================

	Mu2eHardware.getDeviceType = function (uid) {
		if (!_feStructure || !_feStructure.apps) return "other";
		for (var i = 0; i < _feStructure.apps.length; ++i) {
			var app = _feStructure.apps[i];
			for (var j = 0; j < app.dtcs.length; ++j) {
				if (app.dtcs[j].name === uid) return "dtc";
				for (var k = 0; k < app.dtcs[j].rocs.length; ++k) {
					if (app.dtcs[j].rocs[k].name === uid) return "roc";
				}
			}
		}
		return "other";
	};

	// =========================================================================
	// getROCsForDTC — return array of {name, enabled, linkIndex} for a DTC
	// =========================================================================

	Mu2eHardware.getROCsForDTC = function (dtcUID) {
		if (!_feStructure || !_feStructure.apps) return [];
		for (var i = 0; i < _feStructure.apps.length; ++i) {
			var app = _feStructure.apps[i];
			for (var j = 0; j < app.dtcs.length; ++j) {
				if (app.dtcs[j].name === dtcUID) {
					var result = [];
					for (var k = 0; k < app.dtcs[j].rocs.length; ++k) {
						var roc = app.dtcs[j].rocs[k];
						result.push({
							name: roc.name,
							enabled: roc.enabled === "1",
							linkIndex: k,
						});
					}
					return result;
				}
			}
		}
		return [];
	};

	// =========================================================================
	// getParentDTC — for a ROC UID, return the parent DTC name (or null)
	// =========================================================================

	Mu2eHardware.getParentDTC = function (rocUID) {
		if (!_feStructure || !_feStructure.apps) return null;
		for (var i = 0; i < _feStructure.apps.length; ++i) {
			var app = _feStructure.apps[i];
			for (var j = 0; j < app.dtcs.length; ++j) {
				for (var k = 0; k < app.dtcs[j].rocs.length; ++k) {
					if (app.dtcs[j].rocs[k].name === rocUID)
						return app.dtcs[j].name;
				}
			}
		}
		return null;
	};

	// =========================================================================
	// getROCLinkIndex — return the link index (position) of a ROC within its DTC
	// =========================================================================

	Mu2eHardware.getROCLinkIndex = function (rocUID) {
		if (!_feStructure || !_feStructure.apps) return -1;
		for (var i = 0; i < _feStructure.apps.length; ++i) {
			var app = _feStructure.apps[i];
			for (var j = 0; j < app.dtcs.length; ++j) {
				for (var k = 0; k < app.dtcs[j].rocs.length; ++k) {
					if (app.dtcs[j].rocs[k].name === rocUID)
						return k;
				}
			}
		}
		return -1;
	};

	// =========================================================================
	// getROCMacros — get ROC-relevant macros from the parent DTC
	//
	//   Returns { dtcUID, macros: {name: macroObj, ...}, linkIndex }
	//   The macros are either "ROC FEMacro - X" (same-type ROCs) or
	//   "Link<N>_<ROC_UID>_X" (mixed-type ROCs), plus DTC-level ROC
	//   commands like "ROC Read", "ROC Write", etc.
	// =========================================================================

	Mu2eHardware.getROCMacros = function (rocUID) {
		var dtcUID = Mu2eHardware.getParentDTC(rocUID);
		if (!dtcUID) return null;

		var dtcMacros = _feToMacrosMap[dtcUID];
		if (!dtcMacros) return null;

		var linkIndex = Mu2eHardware.getROCLinkIndex(rocUID);
		var result = {};

		for (var name in dtcMacros) {
			// "ROC FEMacro - X" pattern (all ROCs same type)
			if (name.indexOf("ROC FEMacro - ") === 0) {
				result[name] = dtcMacros[name];
				continue;
			}
			// "Link<N>_<ROC_UID>_X" pattern (mixed ROC types)
			if (name.indexOf("Link" + linkIndex + "_" + rocUID + "_") === 0) {
				result[name] = dtcMacros[name];
				continue;
			}
			// DTC-level ROC commands
			if (name === "ROC Read" || name === "ROC Write" ||
				name === "ROC Block Read" || name === "ROC Block Write" ||
				name === "ROC Setup" || name === "ROC Firmware Inventory") {
				result[name] = dtcMacros[name];
			}
		}

		return {
			dtcUID: dtcUID,
			macros: result,
			linkIndex: linkIndex,
		};
	};

	// =========================================================================
	// fetchDeviceSettings — query config table fields for a device UID
	//
	//   Uses getFieldsOfRecords with depth to follow links into child tables
	//   (ROC type parameters, slow controls channels, etc.), then fetches
	//   all discovered field values.
	//
	//   callback(fields) where fields = [{name, value, table}, ...] or null
	// =========================================================================

	Mu2eHardware.fetchDeviceSettings = function (uid, callback) {
		var devType = Mu2eHardware.getDeviceType(uid);
		var tableName;

		// DTC UIDs are records of FEInterfaceTable; DTCInterfaceTable holds the
		// linked type record (LinkToFETypeTable), reached by following links.
		if (devType === "dtc")
			tableName = "FEInterfaceTable";
		else if (devType === "roc")
			tableName = "ROCInterfaceTable";
		else {
			if (callback) callback(null);
			return;
		}

		_setConfigGuiLid();
		ConfigurationAPI.getFieldsOfRecords(
			tableName, uid,
			"",  // all fields
			5,   // follow links up to 5 levels deep
			function (fieldObjs) {
				if (!fieldObjs || !fieldObjs.length) {
					if (callback) callback(null);
					return;
				}

				// Filter out noise
				var skipCols = {
					"CommentDescription": 1, "Author": 1,
					"RecordInsertionTime": 1,
				};
				var filtered = [];
				for (var i = 0; i < fieldObjs.length; ++i) {
					var col = fieldObjs[i].fieldColumnName;
					var colType = fieldObjs[i].fieldColumnType || "";
					if (skipCols[col]) continue;
					if (colType.indexOf("GroupID") >= 0) continue;
					if (colType.indexOf("ChildLink") === 0 &&
						colType.indexOf("UID") < 0) continue;
					if (colType.indexOf("ChildLinkGroupID") === 0) continue;
					if (colType === "ChildLinkUID" ||
						colType.indexOf("ChildLinkUID") === 0) continue;
					filtered.push(fieldObjs[i]);
				}

				if (!filtered.length) {
					if (callback) callback(null);
					return;
				}

				_setConfigGuiLid();
				ConfigurationAPI.getFieldValuesForRecords(
					tableName, uid, filtered,
					function (fieldValues, errMsg) {
						if (errMsg || !fieldValues || !fieldValues.length) {
							if (callback) callback(null);
							return;
						}
						var result = [];
						for (var i = 0; i < fieldValues.length; ++i) {
							// Show just the final field name, not full path
							var path = fieldValues[i].fieldPath;
							var lastSlash = path.lastIndexOf("/");
							var shortName = lastSlash >= 0 ?
								path.substring(lastSlash + 1) : path;
							var tbl = filtered[i] ?
								filtered[i].fieldTableName : "";
							result.push({
								name: shortName,
								value: fieldValues[i].fieldValue,
								table: tbl,
							});
						}
						if (callback) callback(result);
					},
					undefined, true
				);
			}
		);
	};

	// =========================================================================
	// fetchChannels — get channel records for a ROC
	//
	//   Tries multiple link paths to find the channels table and group:
	//     1. Tracker path: ROCTypeLinkTable → SubsystemTrackerParametersTable
	//        → LinkToTrackerROCChannelsTable (SubsystemTrackerChannelsTable)
	//     2. Slow controls path: LinkToSlowControlsChannelTable
	//        (FESlowControlsTable)
	//
	//   callback(result) where result = {
	//     channels: [{uid, fields: {name:val, ...}}, ...],
	//     tableName: string
	//   } or null
	// =========================================================================

	// Channel link paths to try, in order
	var _CHANNEL_PATHS = [
		{
			groupField: "ROCTypeLinkTable/LinkToTrackerROCChannelsTableGroupID",
			tableName: "SubsystemTrackerChannelsTable",
			groupCol: "GroupID",
		},
		{
			// CRV: one record per FEB port (Port, Status, Bias, Trim, Threshold, ...)
			groupField: "ROCTypeLinkTable/FEBsGroupID",
			tableName: "SubsystemCRVFebTable",
			groupCol: "ROC",
		},
		{
			groupField: "LinkToSlowControlsChannelGroupID",
			tableName: "FESlowControlsTable",
			groupCol: "FEGroupID",
		},
	];

	// =========================================================================
	// fetchROCLinkIDs — read the configured linkID of each ROC
	//
	//   getROCLinkIndex() returns the ROC's position in the DTC's list, which
	//   is only the link number when links are filled 0,1,2,... in order.
	//   Use this when the real hardware link matters (e.g. CRV on links 0 and 3).
	//
	//   callback(linkMap) where linkMap = { rocUID: linkNumber, ... }
	// =========================================================================

	Mu2eHardware.fetchROCLinkIDs = function (rocUIDs, callback) {
		if (!rocUIDs || !rocUIDs.length) {
			if (callback) callback({});
			return;
		}
		var doFetch = function () {
			_setConfigGuiLid();
			ConfigurationAPI.getFieldValuesForRecords(
				"ROCInterfaceTable", rocUIDs, ["linkID"],
				function (fieldValues, errMsg) {
					if (errMsg)
						Debug.log("Mu2eHardware.fetchROCLinkIDs: " + errMsg, Debug.HIGH_PRIORITY);
					var linkMap = {};
					for (var i = 0; fieldValues && i < fieldValues.length; ++i) {
						var n = parseInt(fieldValues[i].fieldValue, 10);
						if (!isNaN(n)) linkMap[fieldValues[i].fieldUID] = n;
					}
					if (callback) callback(linkMap);
				},
				undefined, true);
		};
		if (_configGuiLid) doFetch();
		else _discoverConfigGuiLid(doFetch);
	};

	Mu2eHardware.fetchChannels = function (uid, callback) {
		if (Mu2eHardware.getDeviceType(uid) !== "roc") {
			if (callback) callback(null);
			return;
		}

		_tryChannelPath(uid, 0, callback);
	};

	function _captureMacroMakerLid() {
		if (!_macroMakerLid && DesktopContent._localUrnLid &&
			DesktopContent._localUrnLid != _configGuiLid)
			_macroMakerLid = DesktopContent._localUrnLid;
	}

	function _setConfigGuiLid() {
		_captureMacroMakerLid();
		if (_configGuiLid) DesktopContent._localUrnLid = _configGuiLid;
	}

	Mu2eHardware.setConfigGuiLid = function () { _setConfigGuiLid(); };
	Mu2eHardware.discoverConfigGuiLid = function (cb) { _discoverConfigGuiLid(cb); };

	function _setMacroMakerLid() {
		_captureMacroMakerLid();
		if (_macroMakerLid)
			DesktopContent._localUrnLid = _macroMakerLid;
	}

	function _tryChannelPath(uid, pathIdx, callback) {
		if (pathIdx >= _CHANNEL_PATHS.length) {
			if (callback) callback(null);
			return;
		}

		var pathInfo = _CHANNEL_PATHS[pathIdx];

		_setConfigGuiLid();
		ConfigurationAPI.getFieldValuesForRecords(
			"ROCInterfaceTable", uid,
			[pathInfo.groupField],
			function (fieldValues, errMsg) {
				var groupID = fieldValues && fieldValues.length ?
					fieldValues[0].fieldValue : null;

				if (!groupID || groupID === "NO_LINK" || groupID === "DEFAULT") {
					_tryChannelPath(uid, pathIdx + 1, callback);
					return;
				}

				_setConfigGuiLid();
				ConfigurationAPI.getSubsetRecords(
					pathInfo.tableName,
					pathInfo.groupCol + "=" + groupID,
					function (records) {
						if (!records || !records.length) {
							_tryChannelPath(uid, pathIdx + 1, callback);
							return;
						}

						_setConfigGuiLid();
						ConfigurationAPI.getFieldsOfRecords(
							pathInfo.tableName, records[0], "", 2 /* follow one child link, e.g. FEB SettingsLink */,
							function (fieldObjs) {
								// Keep the field objects (not just column names) so
								// fields reached through a child link keep their
								// relative path, e.g. "SettingsLink/OnSpillStart".
								var dataFields = [];
								for (var i = 0; fieldObjs && i < fieldObjs.length; ++i) {
									var ct = fieldObjs[i].fieldColumnType || "";
									var cn = fieldObjs[i].fieldColumnName;
									if (cn === "CommentDescription" || cn === "Author" ||
										cn === "RecordInsertionTime") continue;
									if (ct.indexOf("GroupID") >= 0) continue;
									if (ct.indexOf("ChildLink") === 0) continue;
									dataFields.push(fieldObjs[i]);
								}

								_setConfigGuiLid();
								ConfigurationAPI.getFieldValuesForRecords(
									pathInfo.tableName, records, dataFields,
									function (allValues) {
										var channels = [];
										var chanMap = {};
										for (var i = 0; allValues && i < allValues.length; ++i) {
											var v = allValues[i];
											if (!chanMap[v.fieldUID]) {
												chanMap[v.fieldUID] = { uid: v.fieldUID, fields: {} };
												channels.push(chanMap[v.fieldUID]);
											}
											chanMap[v.fieldUID].fields[v.fieldPath] = v.fieldValue;
										}
										if (callback) callback({
											channels: channels,
											tableName: pathInfo.tableName,
										});
									},
									undefined, true
								);
							}
						);
					}
				);
			},
			undefined, true
		);
	}

	// =========================================================================
	// toggleDeviceStatus — purely local: flip status in pending changes map
	//   Instant, no server call. Returns the new status ("1" or "0").
	// =========================================================================

	var _pendingChanges = {};  // uid -> { table, newStatus }

	Mu2eHardware.toggleDeviceStatus = function (uid) {
		var devType = Mu2eHardware.getDeviceType(uid);
		var tableName;
		if (devType === "dtc")
			tableName = "FEInterfaceTable";
		else if (devType === "roc")
			tableName = "ROCInterfaceTable";
		else
			return null;

		if (_pendingChanges[uid]) {
			// Already toggled — flip back or forward
			var cur = _pendingChanges[uid].newStatus;
			var toggled = (cur === "1") ? "0" : "1";
			_pendingChanges[uid].newStatus = toggled;
			// If back to original, remove from pending
			if (toggled === _pendingChanges[uid].origStatus)
				delete _pendingChanges[uid];
			else
				_pendingChanges[uid].newStatus = toggled;
			return toggled;
		}

		// First toggle — read current from structure data
		var origStatus = _getDeviceStatus(uid);
		var newStatus = (origStatus === "1") ? "0" : "1";
		_pendingChanges[uid] = {
			table: tableName,
			origStatus: origStatus,
			newStatus: newStatus,
		};
		return newStatus;
	};

	Mu2eHardware.hasUnsavedChanges = function () {
		return Object.keys(_pendingChanges).length > 0;
	};

	Mu2eHardware.getPendingChanges = function () {
		return _pendingChanges;
	};

	// =========================================================================
	// saveChanges — write all pending status changes to server at once
	//   callback(success)
	// =========================================================================

	Mu2eHardware.saveChanges = function (callback) {
		var uids = Object.keys(_pendingChanges);
		if (!uids.length) {
			if (callback) callback(true);
			return;
		}

		var modifiedTables;
		var idx = 0;

		function writeNext() {
			if (idx >= uids.length) {
				_finishSave(modifiedTables, false, callback);
				return;
			}

			var uid = uids[idx];
			var ch = _pendingChanges[uid];
			idx++;

			_setConfigGuiLid();
			ConfigurationAPI.setFieldValuesForRecords(
				ch.table, uid, ["Status"], [ch.newStatus],
				function (mt) {
					if (!mt || !mt.length) {
						_finishSave(null, true, callback);
						return;
					}
					modifiedTables = mt;
					writeNext();
				},
				modifiedTables
			);
		}

		writeNext();
	};

	function _finishSave(modifiedTables, hadError, callback) {
		if (hadError || !modifiedTables) {
			Debug.log("Mu2eHardware.saveChanges: error writing changes.",
				Debug.HIGH_PRIORITY);
			if (callback) callback(false);
			return;
		}

		_setConfigGuiLid();
		ConfigurationAPI.saveModifiedTables(
			modifiedTables,
			function (savedTables, savedGroups, savedAliases) {
				if (!savedTables || !savedTables.length) {
					Debug.log("Mu2eHardware.saveChanges: error saving.",
						Debug.HIGH_PRIORITY);
					if (callback) callback(false);
					return;
				}

				Debug.log("Mu2eHardware: saved " + savedTables.length +
					" table(s), " + savedGroups.length + " group(s), " +
					savedAliases.length + " alias(es).");

				_pendingChanges = {};
				if (callback) callback(true);
			}
		);
	}

	function _getDeviceStatus(uid) {
		if (!_feStructure || !_feStructure.apps) return "1";
		for (var i = 0; i < _feStructure.apps.length; ++i) {
			var app = _feStructure.apps[i];
			for (var j = 0; j < app.dtcs.length; ++j) {
				if (app.dtcs[j].name === uid) return app.dtcs[j].enabled;
				for (var k = 0; k < app.dtcs[j].rocs.length; ++k) {
					if (app.dtcs[j].rocs[k].name === uid)
						return app.dtcs[j].rocs[k].enabled;
				}
			}
		}
		return "1";
	}

	// =========================================================================
	// buildTreeHTML — generate the hardware tree as an HTML string
	//
	//   Returns an HTML string showing:
	//     Section 1: DTC/ROC hierarchy (from structure data)
	//     Section 2: Other FE interfaces (from macro list, not in structure)
	//
	//   onSelectAttr: the onclick attribute string for device nodes,
	//     with __UID__ as placeholder, e.g. "selectDevice('__UID__', event)"
	// =========================================================================

	Mu2eHardware.buildTreeHTML = function (onSelectAttr) {
		var html = "";
		var structure = _feStructure;
		var dtcUIDs = {};

		// Section 1: DTC/ROC hierarchy
		if (structure && structure.apps && structure.apps.length) {
			html += "<div class='tree-section-header'>DTC / ROC Hierarchy</div>";

			for (var i = 0; i < structure.apps.length; ++i) {
				var app = structure.apps[i];
				var appId = "app-" + i;
				html += "<div class='tree-context' onclick='Mu2eHardware.toggleTreeNode(\"" + appId + "\", this)'>";
				html += "<span class='tree-arrow'>&#9660;</span>";
				html += "<span class='status-dot " +
					(app.enabled === "1" ? "status-on" : "status-off") + "'></span>";
				html += "<span>" + _esc(app.name) + "</span>";
				html += "</div>";
				html += "<div class='tree-context-children' id='" + appId + "'>";

				for (var j = 0; j < app.dtcs.length; ++j) {
					var dtc = app.dtcs[j];
					var dtcId = appId + "-dtc-" + j;
					dtcUIDs[dtc.name] = true;

					html += "<div class='tree-dtc' " +
						"id='node-" + _esc(dtc.name) + "' " +
						"onclick='" + onSelectAttr.replace(/__UID__/g, _escAttr(dtc.name)) + "'>";
					html += "<span class='tree-arrow' " +
						"onclick='Mu2eHardware.toggleTreeNode(\"" + dtcId + "\", this.parentElement); event.stopPropagation();'>&#9660;</span>";
					html += "<span class='status-dot " +
						(dtc.enabled === "1" ? "status-on" : "status-off") + "'></span>";
					html += "<span>" + _esc(dtc.name) + "</span>";
					html += "</div>";
					html += "<div class='tree-context-children' id='" + dtcId + "'>";

					for (var k = 0; k < dtc.rocs.length; ++k) {
						var roc = dtc.rocs[k];
						dtcUIDs[roc.name] = true;

						html += "<div class='tree-roc' " +
							"id='node-" + _esc(roc.name) + "' " +
							"onclick='" + onSelectAttr.replace(/__UID__/g, _escAttr(roc.name)) + "'>";
						html += "<span class='status-dot " +
							(roc.enabled === "1" ? "status-on" : "status-off") + "'></span>";
						html += "<span>" + _esc(roc.name) + "</span>";
						html += "</div>";
					}
					html += "</div>";
				}
				html += "</div>";
			}
		}

		// Section 2: Other FE interfaces not in the structure tree
		var otherClasses = [];
		var allClasses = Object.keys(_feClassToFEsMap);
		for (var ci = 0; ci < allClasses.length; ++ci) {
			var cls = allClasses[ci];
			var uids = _feClassToFEsMap[cls];
			var hasNew = false;
			for (var u = 0; u < uids.length; ++u) {
				if (!dtcUIDs[uids[u]]) { hasNew = true; break; }
			}
			if (hasNew) otherClasses.push(cls);
		}

		if (otherClasses.length) {
			html += "<div class='tree-section-header'>Other FE Interfaces</div>";

			for (var ci = 0; ci < otherClasses.length; ++ci) {
				var cls = otherClasses[ci];
				var groupId = "fegroup-" + ci;
				html += "<div class='tree-fe-group' onclick='Mu2eHardware.toggleTreeNode(\"" + groupId + "\", this)'>";
				html += "<span class='tree-arrow'>&#9660;</span>";
				html += "<span>" + _esc(cls) + "</span>";
				html += "</div>";
				html += "<div class='tree-context-children' id='" + groupId + "'>";

				var uids = _feClassToFEsMap[cls];
				for (var u = 0; u < uids.length; ++u) {
					if (dtcUIDs[uids[u]]) continue;
					var feUID = uids[u];
					html += "<div class='tree-fe-device' " +
						"id='node-" + _esc(feUID) + "' " +
						"onclick='" + onSelectAttr.replace(/__UID__/g, _escAttr(feUID)) + "'>";
					html += "<span class='status-dot status-unknown'></span>";
					html += "<span>" + _esc(feUID) + "</span>";
					html += "</div>";
				}
				html += "</div>";
			}
		}

		if (!html) {
			html = "<div class='placeholder-text'>" +
				"No hardware found. Is the system configured?" +
				"</div>";
		}

		return html;
	};

	// =========================================================================
	// buildGridHTML — generate a grid/table view of the hardware
	//
	//   Rows = apps (nodes/contexts)
	//   Columns = DTCs, with ROCs shown inside each DTC cell
	//   Other FE types shown in a separate section below
	//
	//   onSelectAttr: onclick string with __UID__ placeholder
	// =========================================================================

	Mu2eHardware.buildGridHTML = function (onSelectAttr, onToggleAttr) {
		var html = "";
		var structure = _feStructure;
		var dtcUIDs = {};

		if (structure && structure.apps && structure.apps.length) {

			// Find the max number of DTCs across all apps (for column count)
			var maxDtcs = 0;
			for (var i = 0; i < structure.apps.length; ++i) {
				if (structure.apps[i].dtcs.length > maxDtcs)
					maxDtcs = structure.apps[i].dtcs.length;
			}

			html += "<table class='hw-grid'>";

			// Header row
			html += "<thead><tr>";
			html += "<th class='hw-grid-node-header'>Node</th>";
			for (var d = 0; d < maxDtcs; ++d)
				html += "<th class='hw-grid-dtc-header'>DTC " + d + "</th>";
			html += "</tr></thead>";

			html += "<tbody>";
			for (var i = 0; i < structure.apps.length; ++i) {
				var app = structure.apps[i];
				var appOn = app.enabled === "1";
				var statusCls = appOn ? "status-on" : "status-off";

				// Extract short hostname from the first DTC's parentApp field
				// parentApp format: "https://mu2edaq09.fnal.gov:2015/ctx/app"
				var nodeName = app.name;
				if (app.dtcs.length && app.dtcs[0].parentApp) {
					nodeName = _extractHostname(app.dtcs[0].parentApp);
				}

				html += "<tr>";

				// Node/App cell — compact
				html += "<td class='hw-grid-node'>";
				html += "<span class='status-dot " + statusCls + "'></span> ";
				html += "<span>" + _esc(nodeName) + "</span>";
				html += "</td>";

				// DTC cells
				for (var j = 0; j < maxDtcs; ++j) {
					if (j < app.dtcs.length) {
						var dtc = app.dtcs[j];
						dtcUIDs[dtc.name] = true;

						var dtcSelfOn = dtc.enabled === "1";
						var dtcEffective = appOn && dtcSelfOn;
						var dtcStatusCls = dtcSelfOn ? "status-on" : "status-off";

						html += "<td class='hw-grid-dtc-cell" +
							(!dtcEffective ? " hw-grid-cell-disabled" : "") + "'>";
						// Dot is outside the clickable device div so clicks don't conflict
						html += _statusDotHTML(dtc.name, dtcStatusCls, onToggleAttr);
						html += "<span class='hw-grid-dtc' " +
							"id='node-" + _esc(dtc.name) + "' " +
							"onclick='" + onSelectAttr.replace(/__UID__/g, _escAttr(dtc.name)) + "'>";
						html += _esc(dtc.name);
						html += "</span>";

						// ROCs inside this DTC cell
						if (dtc.rocs.length) {
							html += "<div class='hw-grid-rocs'>";
							for (var k = 0; k < dtc.rocs.length; ++k) {
								var roc = dtc.rocs[k];
								dtcUIDs[roc.name] = true;

								var rocSelfOn = roc.enabled === "1";
								var rocStatusCls = rocSelfOn ? "status-on" : "status-off";

								html += "<div class='hw-grid-roc" +
									(!dtcEffective ? " hw-grid-item-disabled" : "") + "'>";
								html += _statusDotHTML(roc.name, rocStatusCls, onToggleAttr);
								html += "<span " +
									"id='node-" + _esc(roc.name) + "' " +
									"onclick='" + onSelectAttr.replace(/__UID__/g, _escAttr(roc.name)) + "'>";
								html += _esc(roc.name);
								html += "</span>";
								html += "</div>";
							}
							html += "</div>";
						}
						html += "</td>";
					} else {
						html += "<td class='hw-grid-empty'></td>";
					}
				}
				html += "</tr>";
			}
			html += "</tbody></table>";
		}

		// Other FE interfaces section
		var otherClasses = [];
		var allClasses = Object.keys(_feClassToFEsMap);
		for (var ci = 0; ci < allClasses.length; ++ci) {
			var cls = allClasses[ci];
			var uids = _feClassToFEsMap[cls];
			var hasNew = false;
			for (var u = 0; u < uids.length; ++u) {
				if (!dtcUIDs[uids[u]]) { hasNew = true; break; }
			}
			if (hasNew) otherClasses.push(cls);
		}

		if (otherClasses.length) {
			html += "<div class='hw-grid-other-header'>Other FE Interfaces</div>";
			html += "<div class='hw-grid-other'>";
			for (var ci = 0; ci < otherClasses.length; ++ci) {
				var cls = otherClasses[ci];
				html += "<div class='hw-grid-other-group'>";
				html += "<div class='hw-grid-other-class'>" + _esc(cls) + "</div>";

				var uids = _feClassToFEsMap[cls];
				for (var u = 0; u < uids.length; ++u) {
					if (dtcUIDs[uids[u]]) continue;
					var feUID = uids[u];
					html += "<div class='hw-grid-other-device' " +
						"id='node-" + _esc(feUID) + "' " +
						"onclick='" + onSelectAttr.replace(/__UID__/g, _escAttr(feUID)) + "'>";
					html += "<span class='status-dot status-unknown'></span> ";
					html += "<span>" + _esc(feUID) + "</span>";
					html += "</div>";
				}
				html += "</div>";
			}
			html += "</div>";
		}

		if (!html) {
			html = "<div class='placeholder-text'>" +
				"No hardware found. Is the system configured?" +
				"</div>";
		}

		return html;
	};

	// =========================================================================
	// toggleTreeNode — expand/collapse a collapsible tree section
	// =========================================================================

	Mu2eHardware.toggleTreeNode = function (childId, parentEl) {
		var children = document.getElementById(childId);
		if (!children) return;

		var arrow = parentEl.querySelector(".tree-arrow");
		if (children.style.display === "none") {
			children.style.display = "";
			if (arrow) arrow.classList.remove("collapsed");
		} else {
			children.style.display = "none";
			if (arrow) arrow.classList.add("collapsed");
		}
	};

	// =========================================================================
	// highlightTreeNode — set the selected highlight on a tree node
	// =========================================================================

	Mu2eHardware.highlightTreeNode = function (uid) {
		var oldSel = document.querySelector(".tree-node-selected");
		if (oldSel) oldSel.classList.remove("tree-node-selected");

		if (uid) {
			var node = document.getElementById("node-" + uid);
			if (node) node.classList.add("tree-node-selected");
		}
	};

	// =========================================================================
	// formatMacroResult — render a macro result object as HTML
	//
	//   macroName: the macro that was run
	//   result:    the result object from runMacro callback
	//   deviceUID: the target device UID (for display if result lacks it)
	//   Returns an HTML string
	// =========================================================================

	Mu2eHardware.formatMacroResult = function (macroName, result, deviceUID) {
		if (result.error) {
			return "<span class='badValue'>Error: " + _esc(result.error) + "</span>";
		}

		var str = "";
		var now = new Date();
		str += "Completed: " + now.toLocaleTimeString() + "<br>";
		str += "Macro: <b>" + _esc(macroName) + "</b><br>";
		str += result.targets.length + " target(s).<br>";

		for (var f = 0; f < result.targets.length; ++f) {
			var t = result.targets[f];
			if (result.targets.length > 1 || t.hostname) {
				str += "<hr style='border:none; border-top:1px solid #ddd; margin:6px 0;'>";
				str += "<b>Target: " + _esc(t.uid || deviceUID);
				if (t.hostname)
					str += " (" + _esc(t.hostname) + ":" +
						_esc(t.context || "") + "/" + _esc(t.supervisor || "") + ")";
				str += "</b><br>";
			}

			var outKeys = Object.keys(t.outputs);
			for (var i = 0; i < outKeys.length; ++i) {
				var name = outKeys[i];
				var value = t.outputs[name];
				str += "<b>" + _esc(name) + "</b> = " +
					Mu2eHardware.colorizeValue(_esc(value)) + "<br>";
			}
			if (!outKeys.length)
				str += "<i>(no output arguments)</i><br>";
		}

		if (!result.targets.length)
			str += "<i>(no response data)</i><br>";

		return str;
	};

	// =========================================================================
	// colorizeValue — apply status color coding to an output value string
	// =========================================================================

	Mu2eHardware.colorizeValue = function (value) {
		// Detect array-like values and render as bitmap
		var bitmap = _tryParseBitArray(value);
		if (bitmap)
			return Mu2eHardware.renderBitmap(bitmap);

		return value
			.replace(/OK/g, "<span class='goodValue'>OK</span>")
			.replace(/DONE/g, "<span class='goodValue'>DONE</span>")
			.replace(/GOOD/g, "<span class='goodValue'>GOOD</span>")
			.replace(/LOCKED/g, "<span class='goodValue'>LOCKED</span>")
			.replace(/DEAD/g, "<span class='badValue'>DEAD</span>")
			.replace(/FAILED/g, "<span class='badValue'>FAILED</span>")
			.replace(/FAIL/g, "<span class='badValue'>FAIL</span>")
			.replace(/ERROR/g, "<span class='badValue'>ERROR</span>")
			.replace(/MISSING/g, "<span class='badValue'>MISSING</span>")
			.replace(/BAD/g, "<span class='badValue'>BAD</span>");
	};

	// =========================================================================
	// renderBitmap — render an array of 0/1 values as a compact color grid
	//
	//   bits: array of numbers (0 or 1)
	//   Returns HTML string
	// =========================================================================

	Mu2eHardware.renderBitmap = function (bits) {
		var html = "<div class='bitmap-grid'>";
		for (var i = 0; i < bits.length; ++i) {
			var on = (bits[i] == 1);
			html += "<span class='bitmap-cell " +
				(on ? "bitmap-on" : "bitmap-off") +
				"' title='ch " + i + ": " + (on ? "ON" : "OFF") + "'></span>";
		}
		html += "</div>";
		return html;
	};

	// =========================================================================
	// Promoted macros config — which macros to show as quick-action buttons
	// =========================================================================

	Mu2eHardware.PROMOTED_MACROS = {
		"DTCFrontEndInterface": [
			"DTC Soft Reset",
			"Get Simple Status",
			"Get DTC Counters",
			"Get Firmware Version",
			"Get Link Lock Status",
			"DTC Read",
			"DTC Write",
			"ROC Read",
			"ROC Write",
		],
	};

	Mu2eHardware.ROC_PROMOTED = [
		"ROC FEMacro - Print Status",
		"ROC FEMacro - Reset and configure",
		"ROC FEMacro - Read Register",
		"ROC FEMacro - Write Register",
		"ROC FEMacro - Digi Read",
		"ROC FEMacro - Digi Write",
	];

	// =========================================================================
	// macroDisplayName — strip ROC FEMacro prefix for cleaner button labels
	// =========================================================================

	Mu2eHardware.macroDisplayName = function (name) {
		if (name.indexOf("ROC FEMacro - ") === 0)
			return name.substring(14);
		if (name.indexOf("ROC FEMacro -") === 0)
			return name.substring(13).trim();
		var linkMatch = name.match(/^Link\d+_[^_]+_(.+)$/);
		if (linkMatch) return linkMatch[1];
		return name;
	};

	// =========================================================================
	// renderSettingsHTML — render settings fields as grouped accordion HTML
	//   fields: [{name, value, table}, ...]
	//   Returns HTML string
	// =========================================================================

	Mu2eHardware.renderSettingsHTML = function (fields) {
		if (!fields || !fields.length)
			return "<span class='placeholder-text'>No settings available.</span>";

		var groups = [];
		var groupMap = {};
		for (var i = 0; i < fields.length; ++i) {
			var tbl = fields[i].table || "Settings";
			if (!groupMap[tbl]) {
				groupMap[tbl] = [];
				groups.push(tbl);
			}
			groupMap[tbl].push(fields[i]);
		}

		var h = "";
		for (var g = 0; g < groups.length; ++g) {
			var gName = groups[g];
			var gFields = groupMap[gName];
			var open = g === 0 ? " accordion-open" : "";
			var displayName = gName.replace(/Table$/, "");

			h += "<div class='accordion-section'>";
			h += "<div class='accordion-header' onclick='this.nextElementSibling.classList.toggle(\"accordion-open\")'>" +
				displayName + " <span class='accordion-count'>(" +
				gFields.length + ")</span></div>";
			h += "<div class='accordion-body" + open + "'>";
			h += "<table class='settings-table'>";
			for (var i = 0; i < gFields.length; ++i) {
				h += "<tr><td class='settings-name'>" + _esc(gFields[i].name) +
					"</td><td class='settings-value'>" +
					Mu2eHardware.colorizeValue(_esc(gFields[i].value)) + "</td></tr>";
			}
			h += "</table></div></div>";
		}
		return h;
	};

	// =========================================================================
	// renderChannelsHTML — render channel records as accordion table HTML
	//   result: { channels: [{uid, fields: {name:val}}, ...], tableName }
	//   Returns HTML string
	// =========================================================================

	Mu2eHardware.renderChannelsHTML = function (result) {
		if (!result || !result.channels || !result.channels.length)
			return "";

		var chans = result.channels;
		var cols = Object.keys(chans[0].fields);

		var h = "<div class='accordion-section'>";
		h += "<div class='accordion-header' onclick='this.nextElementSibling.classList.toggle(\"accordion-open\")'>" +
			"Channels <span class='accordion-count'>(" +
			chans.length + ")</span></div>";
		h += "<div class='accordion-body' id='channelsBody'>";
		h += "<table class='settings-table'>";
		h += "<tr>";
		for (var c = 0; c < cols.length; ++c)
			h += "<td class='settings-name' style='font-size:9px;'>" +
				_esc(cols[c]) + "</td>";
		h += "</tr>";
		for (var i = 0; i < chans.length; ++i) {
			h += "<tr>";
			for (var c = 0; c < cols.length; ++c)
				h += "<td class='settings-value'>" +
					_esc(chans[i].fields[cols[c]] || "") + "</td>";
			h += "</tr>";
		}
		h += "</table></div></div>";
		return h;
	};

	// =========================================================================
	// Internal helpers
	// =========================================================================

	// Try to parse a string like "[ [1,1,0,1,...] ]" or "[1,0,1,...]" into an array of numbers
	function _tryParseBitArray(str) {
		if (!str || str.length < 3) return null;
		var s = str.trim();
		if (s[0] !== "[") return null;
		try {
			var arr = JSON.parse(s);
			// Unwrap nested array: [[1,1,0,...]]
			if (Array.isArray(arr) && arr.length === 1 && Array.isArray(arr[0]))
				arr = arr[0];
			if (!Array.isArray(arr) || arr.length < 2) return null;
			// Check it's all 0s and 1s (or small ints)
			for (var i = 0; i < arr.length; ++i)
				if (typeof arr[i] !== "number") return null;
			return arr;
		} catch (e) {
			return null;
		}
	}

	function _esc(str) {
		if (!str) return "";
		var div = document.createElement("div");
		div.appendChild(document.createTextNode(str));
		return div.innerHTML;
	}

	function _escAttr(str) {
		return _esc(str).replace(/'/g, "&#39;").replace(/"/g, "&quot;");
	}

	function _statusDotHTML(uid, statusCls, onToggleAttr) {
		if (onToggleAttr) {
			return "<span class='status-dot status-dot-clickable " + statusCls + "' " +
				"id='dot-" + _esc(uid) + "' " +
				"onclick='" + onToggleAttr.replace(/__UID__/g, _escAttr(uid)) +
				"; event.stopPropagation();' " +
				"title='Click to toggle on/off'></span> ";
		}
		return "<span class='status-dot " + statusCls + "'></span> ";
	}

	// decodeURIComponent that returns the input unchanged on malformed sequences
	function _decodeURIComponentSafe(value) {
		try {
			return decodeURIComponent(value || "");
		} catch (e) {
			return value || "";
		}
	}

	// Extract short hostname from parentApp string
	// e.g. "https://mu2edaq09.fnal.gov:2015/ctx/app" → "mu2edaq09"
	Mu2eHardware.extractHostname = _extractHostname;

	function _extractHostname(parentApp) {
		var host = parentApp;
		// strip protocol
		var protoEnd = host.indexOf("://");
		if (protoEnd >= 0) host = host.substring(protoEnd + 3);
		// strip port and path
		var portOrPath = host.search(/[:/]/);
		if (portOrPath >= 0) host = host.substring(0, portOrPath);
		// strip common domain suffixes
		host = host.replace(/\.fnal\.gov$/i, "")
			.replace(/\.local$/i, "");
		return host;
	}

	function _discoverMacroMakerLid(callback) {
		DesktopContent.XMLHttpRequest(
			"Request?RequestType=getAppId&classNeedle=ots::MacroMakerSupervisor",
			"",
			function (req) {
				if (!req || !req.responseXML) {
					Debug.log("Mu2eHardware: Could not discover MacroMaker supervisor.",
						Debug.HIGH_PRIORITY);
					if (callback) callback();
					return;
				}
				_macroMakerLid = DesktopContent.getXMLValue(req, "id") | 0;
				Debug.log("Mu2eHardware: MacroMaker LID = " + _macroMakerLid);
				if (callback) callback();
			},
			0, 0, true, false, true /* targetGatewaySupervisor */);
	}

	function _discoverConfigGuiLid(callback) {
		DesktopContent.XMLHttpRequest(
			"Request?RequestType=getAppId&classNeedle=ots::ConfigurationGUISupervisor",
			"",
			function (req) {
				if (!req || !req.responseXML) {
					Debug.log("Mu2eHardware: Could not discover ConfigGUI supervisor.",
						Debug.HIGH_PRIORITY);
					if (callback) callback();
					return;
				}
				_configGuiLid = DesktopContent.getXMLValue(req, "id") | 0;
				Debug.log("Mu2eHardware: ConfigGUI LID = " + _configGuiLid);
				if (callback) callback();
			},
			0, 0, true, false, true /* targetGatewaySupervisor */);
	}

})();
