import { useState, useRef, useEffect, useCallback } from 'react'
import {
  ArrowLeft, RefreshCw, Search, Shield,
  Folder, FileText, LayoutGrid, LayoutList,
  Eye, EyeOff, Loader, Download, X,
  CheckCircle, AlertCircle,
} from 'lucide-react'
import { useSnapshot, _cache, HiddenSummary } from '../Target'

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

export function formatSize(bytes) {
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

export function formatPreview(raw, path) {
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
    const nameIdx = parts.findIndex((p, i) => i >= 5 && /^\d{1,2}:\d{2}$/.test(p))
    let raw = (nameIdx >= 0 ? parts.slice(nameIdx + 1) : parts.slice(8)).join(' ').trim()
    if (!raw || raw === '.' || raw === '..') return null
    const isLink = perms[0] === 'l'
    let name = raw, linkTarget = null
    if (isLink) {
      const arrow = raw.indexOf(' -> ')
      if (arrow >= 0) { name = raw.slice(0, arrow); linkTarget = raw.slice(arrow + 4) }
    }
    return { name, type: isLink ? 'l' : perms[0] === 'd' ? 'd' : 'f', size, perms, linkTarget }
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

export function FilesTab({ target: t, cmd, query, execCmd, startDownload, downloads, busy }) {
  const [cwd, setCwd] = useState('/')
  const [pathInput, setPathInput] = useState('/')
  const [pathEditing, setPathEditing] = useState(false)
  const [pathError, setPathError] = useState(false)
  const [selected, setSelected] = useState(null)
  const [panel, setPanel] = useState('explorer')
  const [view, setView] = useState('list')
  const [fileFilter, setFileFilter] = useState('')
  const [preview, setPreview] = useState(null)

  const doPreview = useCallback((path) => {
    if (!startDownload) return
    setSelected(path.split('/').pop())
    startDownload(path, t.id).then(r => {
      if (r.ok) {
        const { type, content } = formatPreview(r.raw, path)
        setPreview({ path, content, type, size: r.size, raw: r.raw })
      } else {
        setPreview({ path, content: r.error || 'Could not read file', type: 'error', size: 0 })
      }
    })
  }, [startDownload])
  const snap = useSnapshot(`files:${t.id}:${cwd}`)
  const hiddenFiles = t.hidden?.file || []
  const hiddenContent = t.hidden?.content || []

  const cwdRef = useRef(cwd)
  cwdRef.current = cwd
  const snapRef = useRef(snap)
  snapRef.current = snap
  const pendingNav = useRef(null)
  const fetchDir = useCallback((path, force) => {
    const key = `files:${t.id}:${path}`
    if (!force) {
      const cached = _cache.get(key)
      if (cached?.data?.length) return
    }
    snapRef.current.setLoading(true)
    execCmd(`ls -la --color=never ${path} 2>&1`, t.id).then(result => {
      if (path !== cwdRef.current) return
      const s = snapRef.current
      if (result.ok) {
        s.set(parseShellLs(result.output))
      } else {
        s.fail(result.output || 'failed')
      }
    })
  }, [execCmd, t.id])

  useEffect(() => { pendingNav.current = null; fetchDir(cwd) }, [cwd, fetchDir])

  const navigateTo = (path, force) => {
    const base = pendingNav.current || cwd
    const resolved = resolvePath(path, base)
    if (resolved === base && !force) return
    setSelected(null)
    setPreview(null)
    setFileFilter('')
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
  }).filter(e => !fileFilter || e.name.toLowerCase().includes(fileFilter.toLowerCase()))

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

        {/* Filter */}
        <div className="relative shrink-0">
          <Search size={13} className="absolute left-2.5 top-1/2 -translate-y-1/2 text-fg-3 pointer-events-none" />
          <input className="nb-input pl-7 pr-3 py-1.5 text-sm w-36" placeholder="filter..."
            value={fileFilter} onChange={e => setFileFilter(e.target.value)} />
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
                          else if (isLink && entry.linkTarget) {
                            const resolved = entry.linkTarget.startsWith('/')
                              ? entry.linkTarget
                              : resolvePath(entry.linkTarget, cwd)
                            navigateTo(resolved, true)
                          }
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
                            {isLink && entry.linkTarget && <span className="text-fg-3 font-normal"> -&gt; {entry.linkTarget}</span>}
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
                              else if (isLink && entry.linkTarget) {
                                const resolved = entry.linkTarget.startsWith('/')
                                  ? entry.linkTarget
                                  : resolvePath(entry.linkTarget, cwd)
                                navigateTo(resolved, true)
                              }
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
              {sorted.length}{fileFilter ? `/${(snap.data || []).length}` : ''} item{sorted.length !== 1 ? 's' : ''}
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
                    <div className="flex items-center gap-2 mt-1">
                      <span className="text-[11px] text-fg-3 font-mono">{formatSize(dl.size)}</span>
                      <div className="flex items-center gap-2 ml-auto">
                        <button className="nb-btn px-3 py-1 text-xs flex items-center gap-1.5"
                          onClick={() => setDownloads(s => ({ ...s, [key]: { ...s[key], showPreview: true } }))}
                        >
                          <Eye size={12} /> Preview
                        </button>
                        <button className="nb-btn px-3 py-1 text-xs flex items-center gap-1.5" onClick={() => save(key, dl)}>
                          <Download size={12} /> Save
                        </button>
                      </div>
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
