#!/usr/bin/env python3
"""Draw the two diagrams of the tutorial "Running artdaq in Mu2e" and render them to PNG.

usage: python3 -I make_running_artdaq_in_mu2e_diagrams.py
Writes data-path.svg and dqm-flow.svg here, and renders ../mu2e-tutorials/images/running-artdaq-in-mu2e/*.png
with rsvg-convert (-w 2400). Edit this file or the svg, never the png.
"""
import os, subprocess

BLUE, GREEN, GRAY = "#004C97", "#1f7a4d", "#5b6773"
FILL = {BLUE: "#dce8f6", GREEN: "#dff1e6", GRAY: "#e9edf2"}
here = os.path.dirname(os.path.abspath(__file__))
images = os.path.join(here, '..', 'mu2e-tutorials', 'images', 'running-artdaq-in-mu2e')


def flow(name, boxes, labels, frames):
    """boxes: (title, [role lines], note, color); labels: text lines under each arrow; frames: (first, last, color, title)"""
    W, H, GAP, X0, Y = 230, 172, 112, 30, 70
    total = X0 * 2 + len(boxes) * W + (len(boxes) - 1) * GAP
    s = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="312" viewBox="0 0 %d 312" '
         'font-family="Helvetica Neue, Helvetica, Arial, sans-serif">' % (total, total),
         '<defs><marker id="ah" markerUnits="userSpaceOnUse" markerWidth="16" markerHeight="16" refX="14" refY="8" '
         'orient="auto"><path d="M1,1 L15,8 L1,15 z" fill="#004C97"/></marker></defs>',
         '<rect width="100%" height="100%" fill="#fbfbfb"/>']
    for first, last, color, title in frames:
        x = X0 + first * (W + GAP) - 12
        w = (last - first + 1) * W + (last - first) * GAP + 24
        s.append('<rect x="%d" y="%d" width="%d" height="%d" rx="14" fill="none" stroke="%s" stroke-width="2" '
                 'stroke-dasharray="8 6"/>' % (x, Y - 48, w, H + 96, color))
        s.append('<text x="%d" y="%d" font-size="21" font-style="italic" fill="%s">%s</text>' % (x + 16, Y - 22, color, title))
    for i, (title, role, note, color) in enumerate(boxes):
        x = X0 + i * (W + GAP); cx = x + W / 2
        s.append('<rect x="%d" y="%d" width="%d" height="%d" rx="12" fill="%s" stroke="%s" stroke-width="2.5"/>' % (x, Y, W, H, FILL[color], color))
        s.append('<text x="%d" y="%d" font-size="23" font-weight="bold" text-anchor="middle" fill="#1d2b3a">%s</text>' % (cx, Y + 50, title))
        y = Y + 82
        for line in role:
            s.append('<text x="%d" y="%d" font-size="19" text-anchor="middle" fill="%s" font-weight="bold">%s</text>' % (cx, y, color, line)); y += 22
        y += 8
        words = note.split()
        best = min(range(1, len(words)), key=lambda k: abs(len(' '.join(words[:k])) - len(' '.join(words[k:]))))
        for line in (' '.join(words[:best]), ' '.join(words[best:])):
            s.append('<text x="%d" y="%d" font-size="17" text-anchor="middle" fill="#3b4652">%s</text>' % (cx, y, line)); y += 21
        if i < len(boxes) - 1:
            s.append('<line x1="%d" y1="%d" x2="%d" y2="%d" stroke="#004C97" stroke-width="3" marker-end="url(#ah)"/>' % (x + W + 6, Y + H / 2, x + W + GAP - 4, Y + H / 2))
            for j, t in enumerate(labels[i] or []):
                s.append('<text x="%d" y="%d" font-size="14" text-anchor="middle" fill="#3b4652">%s</text>' % (x + W + GAP / 2, Y + H / 2 + 22 + j * 17, t))
    s.append('</svg>')
    svg = os.path.join(here, name + '.svg')
    open(svg, 'w').write('\n'.join(s))
    subprocess.check_call(['rsvg-convert', '-w', '2400', '-o', os.path.join(images, name + '.png'), svg])


flow('data-path', [
    ("ROC and DTC", ["Readout hardware"], "the DTC is read through the DTC library", GRAY),
    ("BoardReader", ["Mu2eEventReceiver"], "opens the DTC and makes DTCEVT fragments", BLUE),
    ("EventBuilders", ["EventBuilder"], "put the fragments into events", BLUE),
    ("DataLogger", ["DataLogger"], "writes the art files", BLUE),
    ("Dispatcher", ["Dispatcher"], "copies events for online monitors", BLUE),
], [None, None, None, None], [(1, 4, BLUE, "artdaq processes, started by the ARTDAQSupervisor")])

flow('dqm-flow', [
    ("Dispatcher", ["Dispatcher"], "copies events for online monitors", BLUE),
    ("Online monitor", ["art process"], "a Mu2e DQM analyzer fills histograms", BLUE),
    ("Producer", ["TCPDataListener", "Producer"], "puts the packets in a buffer", GREEN),
    ("Consumer", ["DQMMu2e", "HistoConsumer"], "unpacks them into Mu2eHistos", GREEN),
    ("Visualizer window", ["in the desktop"], "LIVE VIEW of the histograms", GRAY),
], [None, ["HistoSender", "over TCP"], ["buffer"], ["live", "histograms"]],
    [(0, 1, BLUE, "artdaq"), (2, 3, GREEN, "otsdaq Visualizer, a Data Manager")])
print('done')
