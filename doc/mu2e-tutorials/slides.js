// Slide viewer for one tutorial page: one 16:9 slide at a time, Prev | Goto # | Next.
// Text shrinks to fit a slide (down to 50%); beyond that the slide body scrolls with a fade hint.
// Keys: Left/Right, PageUp/PageDown, Space, Home/End, N notes, A all slides, F full screen, Esc.
// Images load per slide (current and neighbors), all of them in All-slides mode.
// The slide number is kept in the URL (#5), so links and reloads land on the same slide.
(function () {
  var slides = Array.prototype.slice.call(document.querySelectorAll('.slide'));
  var total = slides.length, cur = 1;
  var stage = document.getElementById('stage');
  var input = document.getElementById('gotoInput');
  var prevBtn = document.getElementById('prevBtn'), nextBtn = document.getElementById('nextBtn');
  var body = document.body;

  function fit() {
    if (body.classList.contains('all-slides')) {
      var w = Math.min(stage.clientWidth - 20, 1100);
      slides.forEach(function (s) { size(s, w); });
      return;
    }
    var s = slides[cur - 1];
    var notes = s.querySelector('.snotes');
    var nh = body.classList.contains('show-notes') && notes ? notes.offsetHeight + 8 : 0;
    var aw = stage.clientWidth - 16, ah = stage.clientHeight - 12 - nh;
    size(s, Math.max(240, Math.min(aw, ah * 16 / 9)));
  }
  // set the slide size, then shrink the text until it fits (down to 60%), else let it scroll
  function size(s, w) {
    var box = s.querySelector('.slidebox'), sb = s.querySelector('.sbody');
    box.style.setProperty('--u', (w / 100) + 'px');
    var fs = 1;
    box.style.setProperty('--fs', fs);
    sb.classList.remove('scroll');
    while (sb.scrollHeight > sb.clientHeight + 1 && fs > 0.5) {
      fs -= 0.05;
      box.style.setProperty('--fs', fs.toFixed(2));
    }
    if (sb.scrollHeight > sb.clientHeight + 1) sb.classList.add('scroll');
  }
  // fade the bottom edge while more text is below; remove it once scrolled to the end
  document.querySelectorAll('.sbody').forEach(function (sb) {
    sb.addEventListener('scroll', function () { sb.classList.toggle('at-end', sb.scrollTop + sb.clientHeight >= sb.scrollHeight - 2); });
  });
  // images carry data-src; load a slide's images only when it is shown or next to the one shown
  function loadImages(s) {
    if (!s) return;
    s.querySelectorAll('img[data-src]').forEach(function (im) {
      im.addEventListener('load', function () { if (im.closest('.slide.active')) fit(); });
      im.src = im.getAttribute('data-src');
      im.removeAttribute('data-src');
    });
  }
  // an embedded video loads and plays (muted, looping) only while its slide is the one shown
  function syncVideos() {
    document.querySelectorAll('.slide video[data-src], .slide video[src]').forEach(function (v) {
      var on = !body.classList.contains('all-slides') && v.closest('.slide.active');
      if (on) {
        if (!v.getAttribute('src')) {
          v.addEventListener('loadedmetadata', function () { fit(); });
          v.src = v.getAttribute('data-src');
        }
        v.play().catch(function () {});
      } else v.pause();
    });
  }
  function show(n) {
    n = Math.max(1, Math.min(total, n | 0));
    if (!body.classList.contains('all-slides')) slides[cur - 1].classList.remove('active');
    cur = n;
    loadImages(slides[cur - 1]); loadImages(slides[cur]); loadImages(slides[cur - 2]);
    slides[cur - 1].classList.add('active');
    input.value = cur;
    prevBtn.disabled = cur === 1;
    nextBtn.disabled = cur === total;
    if (body.classList.contains('all-slides')) slides[cur - 1].scrollIntoView({ block: 'start' });
    history.replaceState(null, '', '#' + cur);
    syncVideos();
    fit();
  }
  function toggle(cls, btnId) {
    var on = body.classList.toggle(cls);
    var b = document.getElementById(btnId);
    if (b) b.classList.toggle('on', on);
    if (cls === 'all-slides' && on) slides.forEach(loadImages);
    if (cls === 'all-slides') {
      slides.forEach(function (s) { s.classList.toggle('active', on || s === slides[cur - 1]); });
      if (!on) slides.forEach(function (s) { s.querySelector('.slidebox').style.removeProperty('--u'); });
    }
    syncVideos();
    fit();
  }
  function fullscreen() {
    if (document.fullscreenElement) document.exitFullscreen();
    else if (document.documentElement.requestFullscreen) document.documentElement.requestFullscreen();
  }
  document.addEventListener('fullscreenchange', function () {
    body.classList.toggle('fullscreen', !!document.fullscreenElement);
    document.getElementById('fsBtn').classList.toggle('on', !!document.fullscreenElement);
    setTimeout(fit, 50);
  });

  prevBtn.addEventListener('click', function () { show(cur - 1); });
  nextBtn.addEventListener('click', function () { show(cur + 1); });
  document.getElementById('gotoBtn').addEventListener('click', function (e) { e.preventDefault(); show(+input.value); });
  input.addEventListener('keydown', function (e) { if (e.key === 'Enter') { show(+input.value); input.blur(); } });
  document.getElementById('notesBtn').addEventListener('click', function () { toggle('show-notes', 'notesBtn'); });
  document.getElementById('allBtn').addEventListener('click', function () { toggle('all-slides', 'allBtn'); });
  document.getElementById('fsBtn').addEventListener('click', fullscreen);
  if (!document.querySelector('.snotes')) document.getElementById('notesBtn').disabled = true;

  // click-to-copy buttons on code boxes: copy the command text exactly as shown
  function copyText(text, done) {
    if (navigator.clipboard && window.isSecureContext) {
      navigator.clipboard.writeText(text).then(done, function () { fallbackCopy(text, done); });
    } else fallbackCopy(text, done);
  }
  function fallbackCopy(text, done) {
    var t = document.createElement('textarea');
    t.value = text; t.setAttribute('readonly', ''); t.style.cssText = 'position:fixed;left:-9999px;top:0';
    document.body.appendChild(t); t.select();
    var ok = false;
    try { ok = document.execCommand('copy'); } catch (err) {}
    document.body.removeChild(t);
    if (ok) done();
  }
  document.addEventListener('click', function (e) {
    var b = e.target.closest ? e.target.closest('.copybtn') : null;
    if (!b) return;
    var code = b.parentNode.querySelector('pre');
    copyText(code.textContent.replace(/\n$/, ''), function () {
      b.classList.add('copied'); b.title = 'Copied';
      setTimeout(function () { b.classList.remove('copied'); b.title = 'Copy to clipboard'; }, 1500);
    });
    b.blur();  // so Space or Enter afterwards does not repeat the copy
  });

  document.addEventListener('keydown', function (e) {
    if (e.target.tagName === 'INPUT' || e.target.className === 'copybtn' || e.ctrlKey || e.metaKey || e.altKey) return;
    var k = e.key;
    if (k === 'ArrowRight' || k === 'PageDown' || k === ' ') { show(cur + 1); e.preventDefault(); }
    else if (k === 'ArrowLeft' || k === 'PageUp') { show(cur - 1); e.preventDefault(); }
    else if (k === 'Home') show(1);
    else if (k === 'End') show(total);
    else if (k === 'n' || k === 'N') toggle('show-notes', 'notesBtn');
    else if (k === 'a' || k === 'A') toggle('all-slides', 'allBtn');
    else if (k === 'f' || k === 'F') fullscreen();
  });
  window.addEventListener('resize', fit);
  window.addEventListener('hashchange', function () { var n = parseInt(location.hash.slice(1), 10); if (n && n !== cur) show(n); });
  // images change the slide height when they load

  slides[0].classList.add('active');
  show(parseInt(location.hash.slice(1), 10) || 1);
})();
