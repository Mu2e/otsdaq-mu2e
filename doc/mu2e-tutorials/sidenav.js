// Left tutorial list: show/hide panel and expand/collapse categories (as on the old tutorials_v2 page).
(function () {
  var body = document.body, btn = document.getElementById('sideNavButton');
  // open on the index page, closed on tutorial pages so the slides get the room; remembered per visit
  var stored = sessionStorage.getItem('otsNavClosed');
  var closed = stored === null ? body.classList.contains('viewer') : stored === '1';
  function apply() {
    body.classList.toggle('nav-closed', closed);
    btn.innerHTML = closed ? '&#8594;' : '&#8592;';
    window.dispatchEvent(new Event('resize'));
  }
  btn.addEventListener('click', function () { closed = !closed; sessionStorage.setItem('otsNavClosed', closed ? '1' : '0'); apply(); });
  document.querySelectorAll('.dropdown-btn').forEach(function (b) {
    b.addEventListener('click', function () {
      b.classList.toggle('active');
      var c = b.nextElementSibling;
      c.style.display = c.style.display === 'block' ? 'none' : 'block';
    });
  });
  apply();
})();

// Tutorial Map (index page): toggle between the cards and the map, remembered per browser; #map opens the map
(function () {
  var bc = document.getElementById('viewCards'), bm = document.getElementById('viewMap');
  var cards = document.getElementById('tutorialCards'), map = document.getElementById('tutorialMap');
  var edges = map ? map.querySelectorAll('.edge') : [];
  if (!bc || !bm || !cards || !map) return;
  function view(m, save) {
    var showMap = m === 'map';
    cards.style.display = showMap ? 'none' : ''; map.style.display = showMap ? 'block' : 'none';
    bc.classList.toggle('on', !showMap); bm.classList.toggle('on', showMap);
    if (save) { try { localStorage.setItem('tutorialView', m); } catch (e) {} history.replaceState(null, '', showMap ? '#map' : location.pathname); }
  }
  bc.addEventListener('click', function () { view('cards', true); });
  bm.addEventListener('click', function () { view('map', true); });
  var saved = null; try { saved = localStorage.getItem('tutorialView'); } catch (e) {}
  view(location.hash === '#map' ? 'map' : (saved === 'map' ? 'map' : 'cards'), false);
  // hovering a key entry (a category, or "draft") highlights the tutorials of that kind and dims the rest
  var nodes = map.querySelectorAll('a.node');
  map.querySelectorAll('.maplegend .lg').forEach(function (lg) {
    function on(e) {
      var cat = lg.getAttribute('data-cat'), draft = lg.getAttribute('data-draft');
      var hit = {};
      nodes.forEach(function (n) {
        var m = cat !== null ? n.getAttribute('data-cat') === cat : n.classList.contains('draft');
        n.classList.toggle('lit', m); n.classList.toggle('faded', !m);
        if (m) hit[n.getAttribute('data-slug')] = 1;
      });
      edges.forEach(function (e) { e.classList.toggle('dim', !(hit[e.getAttribute('data-from')] && hit[e.getAttribute('data-to')])); });
      lg.classList.add('on');
    }
    function off() {
      nodes.forEach(function (n) { n.classList.remove('lit', 'faded'); });
      edges.forEach(function (e) { e.classList.remove('dim', 'hot'); });
      lg.classList.remove('on');
    }
    lg.addEventListener('mouseenter', on); lg.addEventListener('mouseleave', off);
    lg.addEventListener('focus', on); lg.addEventListener('blur', off); lg.tabIndex = 0;
  });
  // hovering a tutorial highlights the arrows into and out of it
  map.querySelectorAll('a.node').forEach(function (n) {
    var s = n.getAttribute('data-slug');
    n.addEventListener('mouseenter', function () { edges.forEach(function (e) {
      var on = e.getAttribute('data-from') === s || e.getAttribute('data-to') === s; e.classList.toggle('hot', on); e.classList.toggle('dim', !on); }); });
    n.addEventListener('mouseleave', function () { edges.forEach(function (e) { e.classList.remove('hot', 'dim'); }); });
  });
})();
