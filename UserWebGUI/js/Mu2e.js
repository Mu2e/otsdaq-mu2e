// Mu2e.js — Shared utilities for all Mu2e OTSDAQ pages.
// Requires: Globals.js, Debug.js, DesktopContent.js loaded first.

var Mu2e = Mu2e || {};

(function () {
	"use strict";

	// =========================================================================
	// State helpers
	// =========================================================================

	var _STATE_CLASSES = {
		"Initial": "state-Initial",
		"Halted": "state-Halted",
		"Configured": "state-Configured",
		"Running": "state-Running",
		"Paused": "state-Paused",
		"Failed": "state-Error",
		"Error": "state-Error",
		"Soft-Error": "state-Error",
		"Shutting-Down": "state-Error",
		"UNKNOWN": "state-unknown",
		// Transition states
		"Configuring": "state-Configured",
		"Starting": "state-Running",
		"Stopping": "state-Halted",
		"Halting": "state-Halted",
		"Initializing": "state-Initial",
		"Resuming": "state-Running",
		"Pausing": "state-Paused",
	};

	Mu2e.stateClass = function (state) {
		if (!state) return "state-unknown";
		if (_STATE_CLASSES[state]) return _STATE_CLASSES[state];
		// Handle "Launching X" style status
		if (state.indexOf("Launching") === 0) return "state-unknown";
		return "state-unknown";
	};

	Mu2e.stateColor = function (state) {
		var colors = {
			"Initial": "#0769bf",
			"Halted": "#ea8303",
			"Configured": "#05947a",
			"Running": "#05942a",
			"Paused": "#05947a",
		};
		return colors[state] || "#6c757d";
	};

	// =========================================================================
	// Time formatting
	// =========================================================================

	Mu2e.formatTime = function (seconds) {
		seconds = seconds | 0;
		var h = Math.floor(seconds / 3600);
		var m = Math.floor((seconds % 3600) / 60);
		var s = seconds % 60;
		return (h < 10 ? "0" : "") + h + ":" +
			(m < 10 ? "0" : "") + m + ":" +
			(s < 10 ? "0" : "") + s;
	};

	// =========================================================================
	// HTML helpers
	// =========================================================================

	Mu2e.esc = function (str) {
		if (!str) return "";
		var div = document.createElement("div");
		div.appendChild(document.createTextNode(str));
		return div.innerHTML;
	};

	Mu2e.escAttr = function (str) {
		return Mu2e.esc(str).replace(/'/g, "&#39;").replace(/"/g, "&quot;");
	};

	// =========================================================================
	// Accordion toggle (works with .accordion-body.accordion-open CSS)
	// =========================================================================

	Mu2e.toggleAccordion = function (headerEl) {
		var body = headerEl.nextElementSibling;
		if (body) body.classList.toggle("accordion-open");
	};

	// =========================================================================
	// decodeDetail — decode %XX sequences and HTML entities from detail strings
	// =========================================================================

	Mu2e.decodeDetail = function (detail) {
		if (!detail) return "";
		var d = detail;
		var prev;
		do {
			prev = d;
			d = d.replace(/%([0-9A-Fa-f]{2})/g, function (m, hex) {
				return String.fromCharCode(parseInt(hex, 16));
			});
		} while (d !== prev);
		d = d.replace(/&apos;/g, "'")
			.replace(/&quot;/g, '"')
			.replace(/&amp;/g, "&")
			.replace(/&lt;/g, "<")
			.replace(/&gt;/g, ">");
		return d;
	};

	// =========================================================================
	// parseUptime — extract uptime from detail string
	// =========================================================================

	Mu2e.parseUptime = function (detail) {
		if (!detail) return "";
		var d = Mu2e.decodeDetail(detail);
		var match = d.match(/Uptime:\s*([^,]+)/);
		return match ? match[1].trim() : "";
	};

	// =========================================================================
	// parseTimeInState — extract time-in-state from detail string
	// =========================================================================

	Mu2e.parseTimeInState = function (detail) {
		if (!detail) return "";
		var d = Mu2e.decodeDetail(detail);
		var match = d.match(/Time-in-state:\s*(\d{1,2}:\d{2}:\d{2})/);
		if (match) return match[1];
		match = d.match(/Time-in-state:\s*(\S+)/);
		if (match) return match[1];
		match = d.match(/^(\d{1,2}:\d{2}:\d{2})\s*-/);
		if (match) return match[1];
		return "";
	};

	// =========================================================================
	// parseComment — extract comment from detail (strip timing prefixes)
	// =========================================================================

	Mu2e.parseComment = function (detail) {
		if (!detail) return "";
		var d = Mu2e.decodeDetail(detail);
		d = d.replace(/^Uptime:\s*[^,]+,\s*Time-in-state:\s*\d{1,2}:\d{2}:\d{2}(\s*-\s*)?/, "");
		d = d.replace(/^Uptime:\s*[^,]+,\s*Time-in-state:\s*\S+(\s*-\s*)?/, "");
		d = d.replace(/^\d{1,2}:\d{2}:\d{2}\s*-\s*/, "");
		return d.trim();
	};

	// =========================================================================
	// formatTimeCell — render time-in-state + uptime + progress as HTML
	// =========================================================================

	Mu2e.formatTimeCell = function (detail, progress) {
		var time = Mu2e.parseTimeInState(detail);
		var uptime = Mu2e.parseUptime(detail);
		progress = progress | 0;
		var h = Mu2e.esc(time);
		if (progress > 0 && progress < 100)
			h += " <span class='ss-progress-pct'>" + progress + "%</span>";
		if (uptime)
			h += "<div class='ss-uptime-sub'>up " + Mu2e.esc(uptime) + "</div>";
		return h;
	};

	// =========================================================================
	// stateCell — render a state cell with progress bar and error icon
	// =========================================================================

	Mu2e.stateCell = function (status, progress, error) {
		var cls = Mu2e.stateClass(status).replace("state-", "ss-bg-");
		var isFailed = (status === "Failed" || status === "Error" ||
			status === "Soft-Error");
		var h = "<div class='ss-progress-bar " + cls + "' " +
			"style='width:" + (progress || 0) + "%'></div>" +
			"<span class='ss-state-text'>" + Mu2e.esc(status || "---");
		if (isFailed && error) {
			var errText = Mu2e.decodeDetail(error).substring(0, 1000);
			h += " <span class='error-icon' " +
				"title='" + Mu2e.escAttr(errText) + "' " +
				"onclick='if(typeof showError===\"function\")showError(\"" +
				Mu2e.escAttr(errText).replace(/"/g, "&quot;") +
				"\");else Debug.err(\"" +
				Mu2e.escAttr(errText).replace(/"/g, "&quot;") +
				"\")'>&#9888;</span>";
		}
		h += "</span>";
		return h;
	};

	// =========================================================================
	// stripStatusError — split "Failed:::|error..." into {status, error}
	// =========================================================================

	Mu2e.stripStatusError = function (rawStatus) {
		if (!rawStatus) return { status: rawStatus || "", error: "" };
		var idx = rawStatus.indexOf(":::");
		if (idx > 0)
			return {
				status: rawStatus.substring(0, idx),
				error: rawStatus.substring(idx + 3),
			};
		return { status: rawStatus, error: "" };
	};

	// =========================================================================
	// parseRunNumber — extract just the number from run number string
	// =========================================================================

	Mu2e.parseRunNumber = function (runNumberStr) {
		if (!runNumberStr) return "---";
		var match = runNumberStr.match(/(\d+)/);
		return match ? match[1] : "---";
	};

	// =========================================================================
	// errorSecondsAgo — seconds since the most recent ots-style timestamp found
	// in an error string (e.g. "Fri Mar  6 10:46:02 2026 CST:"), or -1 if none.
	// Mirrors SubsystemLaunch.extractErrorSecondsAgo so both pages age errors
	// the same way.
	// =========================================================================

	Mu2e.errorSecondsAgo = function (message) {
		if (!message) return -1;
		var m = message.match(
			/([A-Z][a-z]{2} [A-Z][a-z]{2}\s+\d{1,2} \d{2}:\d{2}:\d{2} \d{4})(?:\s+([A-Za-z_+\/-]+|[+-]\d{4}))?:/);
		if (!m) return -1;
		var core = m[1].replace(/\s+/g, " ").trim();
		var tz = m[2] ? m[2].trim() : "";
		var parsed = Date.parse(tz ? (core + " " + tz) : core);
		if (Number.isNaN(parsed)) return -1;
		return Math.floor((Date.now() - parsed) / 1000);
	};

	// =========================================================================
	// timestampSecondsAgo — seconds since an ots timestamp string such as
	// "Tue Sep 15 14:05:59 2026 CDT" (subsystem_lastStatusChangeTime), or -1.
	// =========================================================================

	Mu2e.timestampSecondsAgo = function (timestamp) {
		if (!timestamp || timestamp === "0") return -1;
		var parsed = Date.parse(timestamp);
		if (Number.isNaN(parsed) || parsed <= 0) return -1;
		return Math.floor((Date.now() - parsed) / 1000);
	};

	// =========================================================================
	// formatAgo — "(1h 2m 3s ago)" for a seconds count, or "" for -1
	// =========================================================================

	Mu2e.formatAgo = function (secondsAgo) {
		if (secondsAgo === -1) return "";
		var h = Math.floor(secondsAgo / 3600);
		var m = Math.floor((secondsAgo % 3600) / 60);
		var s = secondsAgo % 60;
		return "(" + h + "h " + m + "m " + s + "s ago)";
	};

	// =========================================================================
	// isErrorStatus — true for Failed / Error / Soft-Error style statuses
	// =========================================================================

	Mu2e.isErrorStatus = function (status) {
		if (!status) return false;
		return status.indexOf("Fail") === 0 ||
			status.indexOf("Error") === 0 ||
			status.indexOf("Soft") === 0;
	};

})();
