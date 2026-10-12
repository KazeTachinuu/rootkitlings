import { useState, useRef, useEffect, useCallback } from 'react'
import {
  ArrowLeft, RefreshCw, Search, Shield, Network,
  SquareTerminal, Folder, FileText, ChevronRight,
  Eye, EyeOff, Loader,
} from 'lucide-react'
import { Badge, StatusPill } from '../components/Badge'
import { CmdBar } from '../components/CmdBar'

const TABS = [
  { key: 'overview', label: 'Overview' },
  { key: 'files', label: 'Files' },
  { key: 'shell', label: 'Shell', icon: SquareTerminal },
  { key: 'log', label: 'Command Log' },
  { key: 'events', label: 'Events' },
]

export function Target({ target: t, log, events, sendCmd, query, onBack }) {
  const [tab, setTab] = useState('overview')

  if (!t) return (
    <div className="flex items-center justify-center h-full">
      <p className="text-fg-3 font-bold text-lg">Target not found</p>
    </div>
  )

  const targetLog = log.filter(l => l.target === t.id)
  const hiddenFiles = t.hidden?.file || []
  const hiddenContent = t.hidden?.content || []
  const tabCounts = {
    overview: null, shell: null,
    files: hiddenFiles.length + hiddenContent.length,
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
        <h1 className="text-xl font-bold truncate">{t.hostname || t.id}</h1>
        <Badge label={t.online ? 'Online' : 'Offline'} color={t.online ? 'green' : 'red'} />
        <div className="hidden sm:flex items-center gap-4 text-xs font-mono text-fg-3 ml-1">
          <span>{t.id}</span>
          <span>{t.exercise}</span>
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
        {tab === 'overview' && <OverviewTab target={t} cmd={cmd} />}
        {tab === 'files' && <FilesTab target={t} cmd={cmd} query={query} />}
        {tab === 'shell' && <ShellTab target={t} cmd={cmd} query={query} />}
        {tab === 'log' && <LogTab entries={targetLog} />}
        {tab === 'events' && <EventsTab events={events} />}
      </div>

      <CmdBar onSend={cmd} />
    </div>
  )
}

/* -- Overview ------------------------------------------------ */

function OverviewTab({ target: t, cmd }) {
  const hidden = {
    file: (t.hidden?.file || []).length,
    pid: (t.hidden?.pid || []).length,
    port: (t.hidden?.port || []).length,
    content: (t.hidden?.content || []).length,
  }
  const total = hidden.file + hidden.pid + hidden.port + hidden.content

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
              <div className="text-xs text-fg-3 mt-1">Survive reboot via boot-load staging</div>
            </div>
            <button
              className="nb-btn px-3 py-1.5 text-sm"
              onClick={() => cmd('PERSIST')}
              disabled={!t.online}
            >
              Toggle
            </button>
          </div>
        </Section>

        <Section title="Actions">
          <div className="flex flex-wrap gap-2">
            {[
              ['STATUS', Network, 'Status'],
              ['PING', RefreshCw, 'Ping'],
            ].map(([c, Icon, label]) => (
              <button key={c} className="nb-btn px-3 py-1.5 text-sm flex items-center gap-1.5" onClick={() => cmd(c)} disabled={!t.online}>
                <Icon size={14} /> {label}
              </button>
            ))}
          </div>
        </Section>
      </div>

      {/* Col 2: hidden items */}
      <div className="border-b-2 lg:border-b-0 lg:border-r-2 border-border p-5 overflow-auto">
        <Section title={<>Hidden Items <span className="text-main ml-1">{total}</span></>}>
          {total > 0 && (
            <button className="nb-btn px-3 py-1.5 text-sm text-red mb-3" onClick={() => cmd('UNHIDE ALL')} disabled={!t.online}>
              Unhide All
            </button>
          )}
          <div className="grid grid-cols-2 gap-3">
            {[
              { label: 'Files', value: hidden.file, color: 'text-main' },
              { label: 'PIDs', value: hidden.pid, color: 'text-main' },
              { label: 'Ports', value: hidden.port, color: 'text-amber' },
              { label: 'Content', value: hidden.content, color: 'text-blue' },
            ].map(s => (
              <div key={s.label} className="nb-card-flat px-4 py-3 flex items-center justify-between">
                <span className="text-xs font-bold uppercase tracking-wider text-fg-3">{s.label}</span>
                <span className={`text-2xl font-bold font-mono ${s.color}`}>{s.value}</span>
              </div>
            ))}
          </div>
        </Section>

      </div>

      {/* Col 3: system info */}
      <div className="p-5 overflow-auto">
        <Section title="System">
          <div className="nb-card-flat overflow-hidden">
            <InfoRow label="Hostname" value={t.hostname || t.id} />
            <InfoRow label="ID" value={t.id} />
            <InfoRow label="Exercise" value={t.exercise} />
            <InfoRow label="Kernel" value={`${t.kernel} ${t.arch}`} />
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

function parseLsReply(reply) {
  if (!reply || !reply.startsWith('LS ')) return null
  const lines = reply.split('\n').slice(1)
  return lines.filter(Boolean).map(line => {
    const [type, sizeStr, ...nameParts] = line.split(' ')
    return {
      name: nameParts.join(' '),
      type: type === 'dir' ? 'd' : 'f',
      size: parseInt(sizeStr, 10) || 0,
    }
  })
}

function FilesTab({ target: t, cmd, query }) {
  const [cwd, setCwd] = useState('/')
  const [pathInput, setPathInput] = useState('/')
  const [pathError, setPathError] = useState(false)
  const [selected, setSelected] = useState(null)
  const [panel, setPanel] = useState('explorer')
  const [entries, setEntries] = useState([])
  const [loading, setLoading] = useState(false)
  const [error, setError] = useState(null)
  const hiddenFiles = t.hidden?.file || []
  const hiddenContent = t.hidden?.content || []

  const fetchDir = useCallback((path) => {
    setLoading(true)
    setError(null)
    setSelected(null)
    query(`LS ${path}`, t.id).then(result => {
      if (result.ok) {
        const parsed = parseLsReply(result.reply)
        if (parsed) {
          setEntries(parsed)
          setError(null)
        } else {
          setEntries([])
          setError('unexpected response')
        }
      } else {
        setEntries([])
        setError(result.reply || 'failed')
      }
      setLoading(false)
    })
  }, [query, t.id])

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
        <button className="nb-btn p-1.5" onClick={() => fetchDir(cwd)} title="Refresh">
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
            {loading ? (
              <div className="flex items-center justify-center gap-2 p-8 text-fg-3">
                <Loader size={16} className="animate-spin" /> Loading...
              </div>
            ) : error ? (
              <div className="p-8 text-center">
                <div className="text-red font-bold mb-1">LS failed</div>
                <div className="text-sm text-fg-3 font-mono">{error}</div>
              </div>
            ) : entries.length ? entries.map(entry => {
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
            <span className="font-mono text-xs">{entries.length} items{selected ? ' / 1 selected' : ''}</span>
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

/* -- Shell --------------------------------------------------- */

function ShellTab({ target: t, cmd, query }) {
  const [input, setInput] = useState('')
  const [history, setHistory] = useState([])
  const [running, setRunning] = useState(false)
  const scrollRef = useRef(null)

  useEffect(() => {
    if (scrollRef.current) scrollRef.current.scrollTop = scrollRef.current.scrollHeight
  }, [history])

  const exec = () => {
    if (!input.trim() || running) return
    const command = input.trim()
    setInput('')
    setRunning(true)
    setHistory(prev => [...prev, { cmd: command, pending: true }])

    query(`EXEC ${command}`, t.id).then(result => {
      let output = result.reply || ''
      let rc = result.ok ? 0 : 1
      if (result.ok && output.startsWith('EXEC ')) {
        output = output.slice(5)
        const lines = output.split('\n')
        const last = lines[lines.length - 1].trim()
        if (/^\d+$/.test(last)) {
          rc = parseInt(last, 10)
          output = lines.slice(0, -1).join('\n')
        }
      }
      setHistory(prev => prev.map(h =>
        h.cmd === command && h.pending
          ? { cmd: command, rc, output, pending: false }
          : h
      ))
      setRunning(false)
    })
  }

  return (
    <div className="flex flex-col h-full">
      <div className="flex items-center gap-2 px-4 py-2.5 border-b-2 border-border bg-bg-2">
        <SquareTerminal size={14} className="text-fg-3" />
        <span className="text-sm font-bold uppercase tracking-wide">Execute Command</span>
        <span className="text-xs text-fg-3 ml-auto">execve on target, returns stdout/stderr + exit code</span>
      </div>

      <div ref={scrollRef} className="flex-1 overflow-auto p-4 bg-[#1a1028] font-mono text-sm space-y-3">
        {!history.length && (
          <div className="text-[#8478a0] italic">No commands executed yet.</div>
        )}
        {history.map((h, i) => (
          <div key={i} className="border-b border-[#2a1f3d] pb-3 last:border-b-0">
            <div className="text-[#a78bfa]"><span className="text-[#7c3aed]">$ </span>{h.cmd}</div>
            {h.pending ? (
              <div className="text-[#8478a0] mt-1 flex items-center gap-2">
                <Loader size={12} className="animate-spin" /> running...
              </div>
            ) : (
              <>
                {h.output && <div className="text-[#e2ddf0] mt-1 whitespace-pre-wrap">{h.output}</div>}
                <div className={`text-xs mt-1 ${h.rc === 0 ? 'text-[#16a34a]' : 'text-[#dc2626]'}`}>
                  exit {h.rc}
                </div>
              </>
            )}
          </div>
        ))}
      </div>

      <div className="flex items-center border-t-2 border-border bg-[#1a1028]">
        <span className="pl-4 font-mono text-sm text-[#7c3aed]">$</span>
        <input
          className="flex-1 bg-transparent border-none outline-none px-2 py-3 font-mono text-sm text-[#e2ddf0] placeholder:text-[#463960]"
          placeholder="id, ls -la /tmp, cat /etc/passwd ..."
          value={input} onChange={e => setInput(e.target.value)}
          onKeyDown={e => e.key === 'Enter' && exec()}
          disabled={!t.online || running}
        />
        <button className="nb-btn-main px-4 py-1.5 text-xs mr-2" onClick={exec} disabled={!t.online || running || !input.trim()}>
          Run
        </button>
      </div>
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
