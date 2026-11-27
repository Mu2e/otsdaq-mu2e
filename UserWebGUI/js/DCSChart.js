//=====================================================================================
//
//	DCSChart.js
//
//	Trend chart of slow controls pvs on a canvas, shared by the DCS monitoring pages
//	(same look as the trend chart of the Alarms page, KafkaAlarms.html).
//	The history of the pvs is read from the archiver database through the
//	getArchiveHistory request of the Slow Controls Dashboard supervisor.
//
//	Note: the page must load DesktopContent.js before this file.
//
//	Example usage:
//
//		var chart = DCSChart.create(parentEl, { onDraw: function (chart, readout) { ... } });
//		chart.series.push({ name: "my pv", color: DCSChart.COLORS[0], points: [] });
//		chart.viewFrom = Date.now() - 3600 * 1000;
//		chart.viewTo = Date.now();
//		DCSChart.fetchHistory(["my pv"], chart.viewFrom / 1000, chart.viewTo / 1000,
//			function (pv, reply, errStr) {
//				if (!errStr) chart.series[0].points = reply.points;
//				DCSChart.draw(chart);
//			});
//
//=====================================================================================

var DCSChart = DCSChart || {}; //define DCSChart namespace

if (typeof DesktopContent == 'undefined')
    alert('ERROR: DesktopContent is undefined! Must include DesktopContent.js before DCSChart.js');

//functions:
//	DCSChart.create(parentEl, options)
//	DCSChart.draw(chart)
//	DCSChart.fetchHistory(pvs, fromSeconds, toSeconds, seriesHandler, doneHandler)
//	DCSChart.niceStep(roughStep)
//	DCSChart.twoDigits(number)
//	DCSChart.timeText(time)
//	DCSChart.valueText(value)
//	DCSChart.valueAt(points, time)
//	DCSChart.keepRange(points, low, high)

DCSChart.COLORS = ["#3987e5", "#e5a139", "#4fc66b", "#e55a5a", "#b07ae8", "#39c8d6", "#e57fc0", "#c9d24a", "#a0a0a0"];
DCSChart.GRID_COLOR = "rgb(60, 65, 75)";
DCSChart.AXIS_COLOR = "rgb(140, 140, 140)";
DCSChart.LABEL_COLOR = "rgb(170, 170, 170)";
DCSChart.SELECTION_COLOR = "rgba(90, 100, 120, 0.4)";
DCSChart.MARGIN = { left: 56, right: 12, top: 8, bottom: 24 };
DCSChart.COMPACT_MARGIN = { left: 28, right: 4, top: 4, bottom: 16 };
DCSChart.TIME_STEPS_SECONDS = [1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600, 7200, 10800, 21600, 43200,
    86400, 172800, 604800, 1209600, 2592000];
DCSChart.MIN_ZOOM_PIXELS = 5;
DCSChart.PARALLEL_REQUESTS = 4; 	//each history request is one database connection at the server

//=====================================================================================
//DCSChart.create ~~
//	returns a new chart, its canvas is appended to parentEl (the size is given by the css).
//	The caller fills chart.series, sets chart.viewFrom and chart.viewTo (ms) and calls DCSChart.draw().
//
//	chart.series is an array of
//		{ name, color, points 		//points: array of [time ms, value|null, minimum, maximum], oldest first
//		[, dashed] 					//dashed line
//		[, group] 					//the series of chart.highlightGroup are highlighted
//		[, hidden] }
//
//	options:
//		compact: 			small chart, with small margins and few labels
//		band: 				also show the range from minimum to maximum of the points
//		highlightNearest: 	highlight the line closest to the mouse
//		onDraw: 			function(chart, readout) called after each draw, e.g. to update a legend,
//								readout is { time, pointerInPlot, values[] (for each series, at time), nearest (series), hasData }
//		onZoom: 			function(fromTime, toTime) called when the user selects a time range with the mouse
DCSChart.create = function (parentEl, options) {
    var chart = {
        options: options || {}, series: [], viewFrom: 0, viewTo: 0, highlightGroup: null,
        lowLimit: NaN, highLimit: NaN, 		//shown as dashed lines when in the visible range
        pointerX: null, pointerY: null, dragFromX: null, plot: null
    };

    var canvas = chart.canvas = parentEl.appendChild(document.createElement("canvas"));
    canvas.className = "dcsChart";

    canvas.onmousemove = function (event) {
        var rectangle = canvas.getBoundingClientRect();
        chart.pointerX = event.clientX - rectangle.left;
        chart.pointerY = event.clientY - rectangle.top;
        DCSChart.draw(chart);
    };
    canvas.onmouseleave = function () {
        chart.pointerX = chart.pointerY = chart.dragFromX = null;
        DCSChart.draw(chart);
    };

    if (chart.options.onZoom) {
        canvas.onmousedown = function (event) {
            if (!chart.plot || event.button != 0) return;
            var x = event.clientX - canvas.getBoundingClientRect().left;
            if (x < chart.plot.left || x > chart.plot.right) return;
            chart.dragFromX = x;
            event.preventDefault(); //do not select text while dragging
        };
        canvas.onmouseup = function (event) {
            if (chart.dragFromX === null) return;
            var plot = chart.plot;
            var x = Math.max(plot.left, Math.min(plot.right, event.clientX - canvas.getBoundingClientRect().left));
            var fromX = Math.min(x, chart.dragFromX), toX = Math.max(x, chart.dragFromX);
            chart.dragFromX = null;

            if (toX - fromX < DCSChart.MIN_ZOOM_PIXELS) { //just a click
                DCSChart.draw(chart);
                return;
            }
            var timePerPixel = (chart.viewTo - chart.viewFrom) / plot.width;
            chart.options.onZoom(chart.viewFrom + (fromX - plot.left) * timePerPixel,
                chart.viewFrom + (toX - plot.left) * timePerPixel);
        };
    }

    return chart;
} //end DCSChart.create()

//=====================================================================================
//DCSChart.draw ~~
//	draws the chart, then calls the onDraw handler
DCSChart.draw = function (chart) {
    var canvas = chart.canvas, options = chart.options;
    var width = canvas.clientWidth, height = canvas.clientHeight;
    if (!width || !height) return;

    var pixelRatio = window.devicePixelRatio || 1;
    if (canvas.width != Math.round(width * pixelRatio) || canvas.height != Math.round(height * pixelRatio)) {
        canvas.width = Math.round(width * pixelRatio);
        canvas.height = Math.round(height * pixelRatio);
    }
    var context = canvas.getContext("2d");
    context.setTransform(pixelRatio, 0, 0, pixelRatio, 0, 0);
    context.clearRect(0, 0, width, height);
    context.font = (options.compact ? "10" : "11") + "px Inconsolata, monospace";

    var margin = options.compact ? DCSChart.COMPACT_MARGIN : DCSChart.MARGIN;
    var plot = chart.plot = {
        left: margin.left, top: margin.top, right: width - margin.right, bottom: height - margin.bottom,
        width: width - margin.left - margin.right, height: height - margin.top - margin.bottom
    };
    var viewFrom = chart.viewFrom, viewTo = chart.viewTo;
    if (!(viewTo > viewFrom) || plot.width <= 0 || plot.height <= 0) return;
    var endOfData = Math.min(Date.now(), viewTo); //the last value holds until now

    //vertical range: the visible values of all the lines, a value holds until the next point
    var minimum = Infinity, maximum = -Infinity;
    chart.series.forEach(function (series) {
        if (series.hidden) return;
        for (var i = 0; i < series.points.length; ++i) {
            var point = series.points[i];
            if (point[1] === null) continue;
            var nextTime = i + 1 < series.points.length ? series.points[i + 1][0] : Infinity;
            if (nextTime < viewFrom || point[0] > viewTo) continue; //not visible
            var hasBand = options.band && typeof point[2] == "number" && typeof point[3] == "number";
            minimum = Math.min(minimum, hasBand ? point[2] : point[1]);
            maximum = Math.max(maximum, hasBand ? point[3] : point[1]);
        }
    });
    var hasData = minimum != Infinity;
    if (!hasData) { minimum = 0; maximum = 1; }
    var pad = minimum == maximum ? Math.max(1, Math.abs(minimum) * 0.01) : (maximum - minimum) * 0.08;
    minimum -= pad;
    maximum += pad;

    function timeToX(time) { return plot.left + (time - viewFrom) / (viewTo - viewFrom) * plot.width; }
    function valueToY(value) { return plot.bottom - (value - minimum) / (maximum - minimum) * plot.height; }

    //horizontal grid lines and value labels
    context.lineWidth = 1;
    context.textBaseline = "middle";
    context.textAlign = "right";
    var valueStep = DCSChart.niceStep((maximum - minimum) / Math.max(2, Math.floor(plot.height / (options.compact ? 28 : 40))));
    for (var tick = Math.ceil(minimum / valueStep) * valueStep; tick <= maximum + valueStep * 1e-9; tick += valueStep) {
        var y = Math.round(valueToY(tick)) + 0.5;
        context.strokeStyle = DCSChart.GRID_COLOR;
        context.beginPath(); context.moveTo(plot.left, y); context.lineTo(plot.right, y); context.stroke();
        context.fillStyle = DCSChart.LABEL_COLOR;
        context.fillText(String(Number(tick.toPrecision(6))), plot.left - 4, y);
    }

    //vertical grid lines and time labels (local time)
    var spanSeconds = (viewTo - viewFrom) / 1000;
    var timeSteps = DCSChart.TIME_STEPS_SECONDS;
    var timeStep = timeSteps[timeSteps.length - 1];
    for (var i = 0; i < timeSteps.length; ++i)
        if (spanSeconds / timeSteps[i] <= Math.max(2, plot.width / (options.compact ? 70 : 100))) { timeStep = timeSteps[i]; break; }
    var timeZoneOffset = new Date(viewFrom).getTimezoneOffset() * 60000;
    context.textAlign = "center";
    context.textBaseline = "top";
    var previousDay = null;
    for (var tick = Math.ceil((viewFrom - timeZoneOffset) / (timeStep * 1000)) * timeStep * 1000 + timeZoneOffset;
        tick <= viewTo; tick += timeStep * 1000) {
        var x = Math.round(timeToX(tick)) + 0.5;
        context.strokeStyle = DCSChart.GRID_COLOR;
        context.beginPath(); context.moveTo(x, plot.top); context.lineTo(x, plot.bottom + 3); context.stroke();

        var date = new Date(tick);
        var day = DCSChart.twoDigits(date.getMonth() + 1) + "-" + DCSChart.twoDigits(date.getDate());
        var label = day;
        if (timeStep < 86400) {
            label = DCSChart.twoDigits(date.getHours()) + ":" + DCSChart.twoDigits(date.getMinutes()) +
                (timeStep < 60 ? ":" + DCSChart.twoDigits(date.getSeconds()) : "");
            if (!options.compact && day != previousDay) label = day + " " + label; //the day, when it changes
        }
        previousDay = day;
        var halfWidth = context.measureText(label).width / 2; //keep the label inside the canvas
        context.fillStyle = DCSChart.LABEL_COLOR;
        context.fillText(label, Math.max(halfWidth, Math.min(width - halfWidth, x)), plot.bottom + (options.compact ? 4 : 7));
    }

    //axes
    context.strokeStyle = DCSChart.AXIS_COLOR;
    context.beginPath();
    context.moveTo(plot.left + 0.5, plot.top);
    context.lineTo(plot.left + 0.5, plot.bottom + 0.5);
    context.lineTo(plot.right, plot.bottom + 0.5);
    context.stroke();

    //the values at the mouse position, or the latest ones, and the line closest to the mouse
    var readout = { time: endOfData, pointerInPlot: false, values: [], nearest: null, hasData: hasData };
    if (chart.pointerX !== null && chart.pointerX >= plot.left && chart.pointerX <= plot.right) {
        readout.pointerInPlot = true;
        readout.time = Math.min(endOfData, viewFrom + (chart.pointerX - plot.left) / plot.width * (viewTo - viewFrom));
    }
    var nearestDistance = Infinity;
    chart.series.forEach(function (series, s) {
        var value = readout.values[s] = DCSChart.valueAt(series.points, readout.time);
        if (!readout.pointerInPlot || value === null || series.hidden) return;
        var distance = Math.abs(valueToY(value) - chart.pointerY);
        if (distance < nearestDistance) { nearestDistance = distance; readout.nearest = series; }
    });

    context.save();
    context.beginPath(); context.rect(plot.left, plot.top, plot.width, plot.height); context.clip();

    //limits of the pv, when they are in the visible range
    [["LOW", chart.lowLimit], ["HIGH", chart.highLimit]].forEach(function (limit) {
        if (!isFinite(limit[1]) || limit[1] <= minimum || limit[1] >= maximum) return;
        var y = Math.round(valueToY(limit[1])) + 0.5;
        context.setLineDash([5, 4]);
        context.strokeStyle = DCSChart.AXIS_COLOR;
        context.beginPath(); context.moveTo(plot.left, y); context.lineTo(plot.right, y); context.stroke();
        context.fillStyle = DCSChart.AXIS_COLOR;
        context.textAlign = "right";
        context.textBaseline = "bottom";
        context.fillText(limit[0] + " " + limit[1], plot.right - 4, y - 2);
    });
    context.setLineDash([]);

    //the lines, drawn in steps since a value holds until the next point, with a gap where the value is null
    context.lineJoin = "round";
    chart.series.forEach(function (series) {
        if (series.hidden) return;

        var highlighted = false, alpha = 1;
        if (chart.highlightGroup !== null) {
            highlighted = series.group === chart.highlightGroup;
            if (!highlighted) alpha = 0.15;
        }
        else if (options.highlightNearest && readout.nearest) {
            highlighted = series == readout.nearest;
            if (!highlighted) alpha = 0.4;
        }

        if (options.band) { //range of the values that were reduced to one point
            context.globalAlpha = alpha * 0.25;
            context.fillStyle = series.color;
            for (var i = 0; i < series.points.length; ++i) {
                var point = series.points[i];
                if (point[1] === null || typeof point[2] != "number" || typeof point[3] != "number") continue;
                var x0 = timeToX(point[0]);
                var x1 = timeToX(i + 1 < series.points.length ? series.points[i + 1][0] : endOfData);
                context.fillRect(x0, valueToY(point[3]), x1 - x0, valueToY(point[2]) - valueToY(point[3]));
            }
        }

        context.globalAlpha = alpha;
        context.lineWidth = highlighted ? 2.5 : 1.5;
        context.strokeStyle = series.color;
        context.setLineDash(series.dashed ? [5, 3] : []);
        context.beginPath();
        var penDown = false;
        for (var i = 0; i < series.points.length; ++i) {
            var point = series.points[i];
            if (point[1] === null) { penDown = false; continue; }
            var x0 = timeToX(point[0]), y = valueToY(point[1]);
            var x1 = timeToX(i + 1 < series.points.length ? series.points[i + 1][0] : endOfData);
            if (penDown) context.lineTo(x0, y); else context.moveTo(x0, y);
            context.lineTo(x1, y);
            penDown = true;
        }
        context.stroke();
    });
    context.globalAlpha = 1;
    context.setLineDash([]);

    //time range being selected with the mouse, or the position of the mouse
    context.lineWidth = 1;
    if (chart.dragFromX !== null && chart.pointerX !== null) {
        var x = Math.max(plot.left, Math.min(plot.right, chart.pointerX));
        context.fillStyle = DCSChart.SELECTION_COLOR;
        context.fillRect(Math.min(x, chart.dragFromX), plot.top, Math.abs(x - chart.dragFromX), plot.height);
    }
    else if (readout.pointerInPlot) {
        var x = Math.round(chart.pointerX) + 0.5;
        context.strokeStyle = DCSChart.AXIS_COLOR;
        context.beginPath(); context.moveTo(x, plot.top); context.lineTo(x, plot.bottom); context.stroke();
    }
    context.restore();

    if (options.onDraw) options.onDraw(chart, readout);
} //end DCSChart.draw()

//=====================================================================================
//DCSChart.fetchHistory ~~
//	reads from the archiver the history of the pvs (array of pv names) in the time range (s since epoch):
//	one request for each pv, DCSChart.PARALLEL_REQUESTS at a time.
//
//	seriesHandler(pv, reply, errStr) is called for each pv: on success errStr is empty and reply is
//		{ points: array of [time ms, value|null, minimum, maximum], oldest first (the first one is the last
//			sample before the time range), units, lowLimit, highLimit, reducedToBuckets, archiveProblem }.
//		After a failure the pvs not yet requested are skipped.
//	doneHandler() is called at the end.
//
//	Returns the batch of requests: set its field cancelled to have no more handlers called.
DCSChart.fetchHistory = function (pvs, fromSeconds, toSeconds, seriesHandler, doneHandler) {
    var batch = { queue: pvs.slice(), pending: 0, cancelled: false, done: false };

    function requestNext() {
        if (batch.cancelled || batch.done) return;

        if (!batch.queue.length) {
            if (!batch.pending) {
                batch.done = true;
                if (doneHandler) doneHandler();
            }
            return;
        }

        var pv = batch.queue.shift();
        ++batch.pending;
        DesktopContent.XMLHttpRequest("Request?RequestType=getArchiveHistory",
            "pv=" + encodeURIComponent(pv) + "&from=" + Math.floor(fromSeconds) + "&to=" + Math.ceil(toSeconds),
            function (req, reqParam, errStr) {
                if (batch.cancelled) return;
                --batch.pending;

                var reply = null;
                if (!errStr) {
                    try {
                        errStr = DesktopContent.getXMLValue(req, "Error") || "";
                        if (!errStr) {
                            reply = JSON.parse(decodeURIComponent(DesktopContent.getXMLValue(req, "JSON")));
                            reply.points.forEach(function (point) { point[0] *= 1000; }); //s to ms
                        }
                    }
                    catch (e) {
                        errStr = "Illegal history response from server: " + e;
                    }
                }

                if (errStr) batch.queue = []; //do not insist
                seriesHandler(pv, errStr ? null : reply, errStr);
                requestNext();
            } /*returnHandler*/,
            0 /*reqParam*/,
            0 /*progressHandler*/,
            true /*callHandlerOnErr*/,
            true /*doNotShowLoadingOverlay*/);
    } //end requestNext()

    for (var i = 0; i < DCSChart.PARALLEL_REQUESTS; ++i)
        requestNext();
    return batch;
} //end DCSChart.fetchHistory()

//=====================================================================================
//DCSChart.niceStep ~~
//	returns the round number (1, 2 or 5 times a power of 10) not less than roughStep
DCSChart.niceStep = function (roughStep) {
    var magnitude = Math.pow(10, Math.floor(Math.log10(roughStep)));
    var fraction = roughStep / magnitude;
    return (fraction <= 1 ? 1 : fraction <= 2 ? 2 : fraction <= 5 ? 5 : 10) * magnitude;
} //end DCSChart.niceStep()

//=====================================================================================
DCSChart.twoDigits = function (number) {
    return String(number).padStart(2, "0");
} //end DCSChart.twoDigits()

//=====================================================================================
//DCSChart.timeText ~~
//	returns the time (ms) as "month-day hours:minutes:seconds", local time
DCSChart.timeText = function (time) {
    var date = new Date(time);
    return DCSChart.twoDigits(date.getMonth() + 1) + "-" + DCSChart.twoDigits(date.getDate()) + " " +
        DCSChart.twoDigits(date.getHours()) + ":" + DCSChart.twoDigits(date.getMinutes()) + ":" +
        DCSChart.twoDigits(date.getSeconds());
} //end DCSChart.timeText()

//=====================================================================================
//DCSChart.valueText ~~
//	returns the value with a reasonable count of digits, or "-" if there is no value
DCSChart.valueText = function (value) {
    if (value === null || value === undefined || isNaN(value)) return "-";
    var size = Math.abs(value);
    if (size != 0 && (size >= 1e6 || size < 1e-3)) return value.toExponential(2);
    return String(Number(value.toFixed(size >= 100 ? 1 : size >= 1 ? 2 : 4)));
} //end DCSChart.valueText()

//=====================================================================================
//DCSChart.valueAt ~~
//	returns the value of the last point not after time, or null
DCSChart.valueAt = function (points, time) {
    var low = 0, high = points.length - 1, found = -1;
    while (low <= high) {
        var middle = (low + high) >> 1;
        if (points[middle][0] <= time) { found = middle; low = middle + 1; }
        else high = middle - 1;
    }
    return found < 0 ? null : points[found][1];
} //end DCSChart.valueAt()

//=====================================================================================
//DCSChart.keepRange ~~
//	removes from the points the values that are not in [low, high], e.g. the bad readings
//	of a sensor: a point with a bad value becomes a gap, and the points are returned
DCSChart.keepRange = function (points, low, high) {
    for (var i = 0; i < points.length; ++i) {
        var point = points[i];
        if (typeof point[1] != "number") continue;
        if (!(point[1] >= low && point[1] <= high))
            point[1] = point[2] = point[3] = null;
        else {
            if (typeof point[2] == "number") point[2] = Math.max(point[2], low);
            if (typeof point[3] == "number") point[3] = Math.min(point[3], high);
        }
    }
    return points;
} //end DCSChart.keepRange()
