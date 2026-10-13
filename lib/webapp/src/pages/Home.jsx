import { Monitor, ChevronRight, Shield, Network, Radio, SquareTerminal, FolderOpen, Activity, Wifi, Eye } from 'lucide-react'
import { API_HOST } from '../api'
import { useState, useEffect, useRef, useCallback } from 'react'
import { createPortal } from 'react-dom'
import { PlatformIcon } from '../components/PlatformIcons'

export function Home({ targets, onNavigate }) {
  const online = targets.filter(t => t.online).length

  return (
    <div className="p-6 max-w-5xl mx-auto space-y-5">
      <NetworkMap targets={targets} onNavigate={onNavigate} />

      <div className="flex items-baseline justify-between">
        <h2 className="text-lg font-bold">Targets</h2>
        <span className="text-xs font-mono text-fg-3 tabular-nums">{online}/{targets.length} online</span>
      </div>

      {!targets.length ? (
        <div className="nb-card p-12 text-center">
          <Monitor size={32} className="mx-auto text-fg-3 mb-3" />
          <p className="font-bold text-fg-2">No targets connected</p>
          <p className="text-sm text-fg-3 font-mono mt-1">Waiting for agents to call back...</p>
        </div>
      ) : (
        <div className="space-y-3">
          {targets.map(t => <TargetCard key={t.id} target={t} onClick={() => onNavigate(t.id)} />)}
        </div>
      )}
    </div>
  )
}

function ContextMenu({ x, y, target, onNavigate, onClose }) {
  const ref = useRef()

  useEffect(() => {
    const handler = (e) => { if (ref.current && !ref.current.contains(e.target)) onClose() }
    const id = requestAnimationFrame(() => {
      document.addEventListener('mousedown', handler)
      document.addEventListener('contextmenu', handler)
    })
    return () => {
      cancelAnimationFrame(id)
      document.removeEventListener('mousedown', handler)
      document.removeEventListener('contextmenu', handler)
    }
  }, [onClose])

  const items = [
    { icon: Eye, label: 'Overview', tab: 'overview' },
    { icon: SquareTerminal, label: 'Shell', tab: 'shell' },
    { icon: FolderOpen, label: 'Files', tab: 'files' },
    { icon: Activity, label: 'Processes', tab: 'procs' },
    { icon: Wifi, label: 'Ports', tab: 'ports' },
  ]

  const name = target.label || (target.hostname && target.hostname !== '(none)' ? target.hostname : target.id)

  return (
    <div ref={ref} className="fixed z-50 border-2 border-border rounded-[var(--radius)] bg-secondary shadow-[4px_4px_0px_0px_var(--color-border)] min-w-[180px] py-1"
      style={{ left: x, top: y }}>
      <div className="px-3 py-1.5 border-b-2 border-border flex items-center gap-2">
        <span className={`w-2 h-2 rounded-full shrink-0 ${target.online ? 'bg-green pulse' : 'bg-red'}`} />
        <span className="text-xs font-bold truncate">{name}</span>
      </div>
      {items.map(({ icon: Icon, label, tab }) => (
        <button key={tab}
          className="w-full px-3 py-1.5 text-left text-sm flex items-center gap-2.5 hover:bg-main hover:text-main-fg transition-colors"
          onClick={() => { onNavigate(target.id, tab); onClose() }}>
          <Icon size={14} />
          {label}
        </button>
      ))}
    </div>
  )
}

function NetworkMap({ targets, onNavigate }) {
  const hasTargets = targets.length > 0
  const [ctx, setCtx] = useState(null)

  const onCtx = useCallback((e, t) => {
    e.preventDefault()
    setCtx({ x: e.clientX, y: e.clientY, target: t })
  }, [])

  return (
    <div className="nb-card overflow-hidden">
      <div className="px-4 py-2 border-b-2 border-border bg-bg-2 flex items-center gap-2">
        <Network size={14} className="text-fg-3" />
        <span className="text-sm font-bold uppercase tracking-wide">Network</span>
        <span className="text-[10px] font-mono text-fg-3 ml-auto tabular-nums">{API_HOST}</span>
      </div>

      <div className="bg-bg px-6 py-8" style={{
        backgroundImage: 'radial-gradient(circle, var(--color-border) 0.8px, transparent 0.8px)',
        backgroundSize: '16px 16px',
      }}>
        <div className="flex flex-col items-center">
          {/* C2 operator node */}
          <div className="border-2 border-border rounded-[var(--radius)] bg-main px-5 py-2.5 shadow-[4px_4px_0px_0px_var(--color-border)] flex items-center gap-2">
            <Radio size={14} className="text-main-fg" />
            <span className="text-sm font-bold text-main-fg uppercase tracking-wider">C2</span>
            <span className="text-xs font-mono text-main-fg/70">{API_HOST}</span>
          </div>

          {/* trunk */}
          {hasTargets && <div className="w-0.5 bg-border" style={{ height: 28 }} />}

          {/* horizontal branch for multiple targets */}
          {hasTargets && targets.length > 1 && (
            <div className="relative w-full flex justify-center" style={{ height: 0 }}>
              <div className="h-0.5 bg-border absolute top-0" style={{
                width: `calc(${((targets.length - 1) / targets.length) * 100}% - 24px)`,
              }} />
            </div>
          )}

          {/* target nodes */}
          {hasTargets ? (
            <div className="flex flex-wrap justify-center gap-4"
              style={targets.length > 1 ? { display: 'grid', gridTemplateColumns: `repeat(${Math.min(targets.length, 4)}, auto)`, gap: '16px', justifyContent: 'center' } : undefined}>
              {targets.map(t => {
                const name = t.label || (t.hostname && t.hostname !== '(none)' ? t.hostname : t.id)
                return (
                  <div key={t.id} className="flex flex-col items-center">
                    {targets.length > 1 && <>
                      <div className={`w-0.5 ${t.online ? 'bg-green' : 'bg-red'}`} style={{ height: 20 }} />
                      <svg width="10" height="6" viewBox="0 0 10 6" className="shrink-0 -mt-px mb-1">
                        <polygon points="0,0 10,0 5,6" fill={t.online ? 'var(--color-green)' : 'var(--color-red)'} />
                      </svg>
                    </>}
                    <button
                      className={`border-2 rounded-[var(--radius)] bg-bg shadow-[2px_2px_0px_0px_var(--color-border)] cursor-pointer
                        transition-all duration-150 hover:translate-y-[-2px] hover:shadow-[2px_4px_0px_0px_var(--color-border)]
                        active:translate-y-[1px] active:shadow-[0px_0px_0px_0px_var(--color-border)]
                        ${t.online ? 'border-green' : 'border-red border-dashed opacity-70'}`}
                      onClick={() => onNavigate(t.id)}
                      onContextMenu={(e) => onCtx(e, t)}
                    >
                      <div className="flex items-center gap-2.5 px-3 py-2">
                        <PlatformIcon kernel={t.kernel} size={24} />
                        <div className="min-w-0">
                          <div className="flex items-center gap-1.5">
                            <span className={`w-1.5 h-1.5 rounded-full shrink-0 ${t.online ? 'bg-green pulse' : 'bg-red'}`} />
                            <span className="font-bold text-xs truncate">{name}</span>
                          </div>
                          <div className="text-[10px] font-mono text-fg-3 truncate">{t.peer || t.id}</div>
                        </div>
                        {(t.module_hidden || t.persisted) && (
                          <div className="flex flex-col gap-0.5 shrink-0">
                            {t.module_hidden && <span className="text-[8px] font-bold bg-green/15 text-green px-1 py-px rounded uppercase">hid</span>}
                            {t.persisted && <span className="text-[8px] font-bold bg-main/15 text-main px-1 py-px rounded uppercase">per</span>}
                          </div>
                        )}
                      </div>
                    </button>
                  </div>
                )
              })}
            </div>
          ) : (
            <div className="text-center py-6 mt-4">
              <Monitor size={28} className="mx-auto text-fg-3 mb-2" />
              <div className="text-sm text-fg-3 font-mono">Waiting for callbacks...</div>
            </div>
          )}
        </div>
      </div>

      {ctx && createPortal(
        <ContextMenu x={ctx.x} y={ctx.y} target={ctx.target} onNavigate={onNavigate} onClose={() => setCtx(null)} />,
        document.body
      )}
    </div>
  )
}

function TargetCard({ target: t, onClick }) {
  const hidden = (t.counts?.files || 0) + (t.counts?.pids || 0) + (t.counts?.content || 0) + (t.counts?.ports || 0)
  const name = t.label || (t.hostname && t.hostname !== '(none)' ? t.hostname : t.id)
  const hiddenParts = [
    t.counts?.files && `${t.counts.files} file${t.counts.files > 1 ? 's' : ''}`,
    t.counts?.pids && `${t.counts.pids} pid${t.counts.pids > 1 ? 's' : ''}`,
    t.counts?.content && `${t.counts.content} content`,
    t.counts?.ports && `${t.counts.ports} port${t.counts.ports > 1 ? 's' : ''}`,
  ].filter(Boolean)

  return (
    <button className="nb-card text-left w-full group" onClick={onClick}>
      <div className="flex items-stretch">
        {/* Left: platform icon column */}
        <div className="flex flex-col items-center justify-center px-5 py-4 border-r-2 border-border bg-bg-2 shrink-0 w-24">
          <PlatformIcon kernel={t.kernel} size={48} />
          <div className="flex items-center gap-1.5 mt-2">
            <span className={`w-2 h-2 rounded-full ${t.online ? 'bg-green pulse' : 'bg-red'}`} />
            <span className={`text-[10px] font-bold uppercase tracking-wider ${t.online ? 'text-green' : 'text-red'}`}>
              {t.online ? 'Online' : 'Offline'}
            </span>
          </div>
        </div>

        {/* Middle: details */}
        <div className="flex-1 min-w-0 px-5 py-4 flex flex-col justify-center gap-1">
          <div className="flex items-baseline gap-3">
            <div className="font-bold text-lg leading-tight truncate">{name}</div>
            <span className="text-[11px] font-mono text-fg-3 shrink-0">{t.id}</span>
          </div>
          <div className="text-xs font-mono text-fg-2">
            {t.kernel || '--'} {t.arch || ''}
            <span className="text-fg-3 mx-2">|</span>
            <span className="text-fg-3">{t.peer || '--'}</span>
          </div>
        </div>

        {/* Right: status bar */}
        <div className="flex items-stretch border-l-2 border-border shrink-0">
          <StatusCell label="Module" value={t.module_hidden ? 'Hidden' : 'Visible'} active={t.module_hidden} />
          <StatusCell label="Persist" value={t.persisted ? 'Yes' : 'No'} active={t.persisted} />
          <StatusCell label="Hidden" value={hidden || '0'} active={hidden > 0} detail={hidden > 0 ? hiddenParts.join(', ') : null} />
          <div className="flex items-center px-3 border-l-2 border-border">
            <ChevronRight size={16} className="text-fg-3 opacity-0 group-hover:opacity-100 transition-opacity" />
          </div>
        </div>
      </div>
    </button>
  )
}

function StatusCell({ label, value, active, detail }) {
  return (
    <div className="flex flex-col items-center justify-center px-4 py-3 border-l-2 border-border first:border-l-0 min-w-[72px]" title={detail || undefined}>
      <span className="text-[9px] font-bold uppercase tracking-widest text-fg-3">{label}</span>
      <span className={`text-sm font-bold font-mono ${active ? 'text-green' : 'text-fg-3'}`}>{value}</span>
    </div>
  )
}
