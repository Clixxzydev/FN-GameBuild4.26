// Copyright Epic Games, Inc. All Rights Reserved.
import * as Path from 'path';
import { BranchState, EdgeState } from './BranchState';
import * as bent from 'bent'
import * as System from './system';
import { Perforce, VERBOSE } from './test-perforce';

const getJson = bent('json')

const colors = require('colors')
colors.enable()
colors.setTheme(require('colors/themes/generic-logging.js'))

// set to true when unshelving independent edge code
export const WORKSPACES_ROOT = process.env.WORKSPACES_ROOT || '/rm_tests/'
const ROBOMERGE_DOMAIN_NAME = process.env.ROBOMERGE_DOMAIN || 'robomerge_functtest'
export const ROBOMERGE_DOMAIN = `http://${ROBOMERGE_DOMAIN_NAME}:8877`
export const DUMMY_SLACK_DOMAIN = `http://${ROBOMERGE_DOMAIN_NAME}:8811`

// export const BOT_NAME = 'FUNCTIONALTEST'
export const OPERATION_URL_TEMPLATE = ROBOMERGE_DOMAIN + `/api/op/bot/<bot>/node/<node>/op/<op>`
export const EDGE_OPERATION_URL_TEMPLATE = ROBOMERGE_DOMAIN + `/api/op/bot/<bot>/node/<node>/edge/<edge>/op/<op>`
export const BRANCH_INFO_URL_TEMPLATE = ROBOMERGE_DOMAIN + `/api/bot/<bot>/branch/<branch>`

export const DEFAULT_BOT_SETTINGS = {
	defaultStreamDepot: 'depot',
	isDefaultBot: true,
	checkIntervalSecs: 1,
	noStreamAliases: true,
	excludeAuthors: ['buildmachine']
}

export async function getRootDataClient(p4: Perforce, ws: string) {
	const client = new P4Client(p4, 'root', ws, WORKSPACES_ROOT + ws, '//RoboMergeData/Main', 'Main')

	await Promise.all([
		client.create(P4Util.specForClient(client)),
		System.mkdir(client.root)
	])
	return client
}

process.on('unhandledRejection', async (err) => {
	if (err) {
		console.log(colors.error(err.toString()))
	}

	console.log(err)
	process.exit(1)
})

export class P4Client {
	constructor(private p4: Perforce, public user: string, public workspace: string, public root: string, public stream: string, public name: string) {
	}

	async create(spec: string) {
		await System.mkdir(WORKSPACES_ROOT + this.workspace)
		await this.p4.client(this.user, spec)
	}

	add(file: string, binary?: boolean) {
		return this.p4.add(this.user, this.workspace, Path.join(this.root, file), binary)
	}

	edit(file: string) {
		return this.p4.edit(this.user, this.workspace, Path.join(this.root, file))
	}

	delete(file: string) {
		return this.p4.delete(this.user, this.workspace, Path.join(this.root, file))
	}

	// submit default changelist
	submit(description: string): Promise<string>

	// submit specified pending changelist
	submit(cl: number): Promise<string>

	submit(arg: any) {
		return this.p4.submit(this.user, this.workspace, arg)
	}

	sync() {
		return this.p4.sync(this.user, this.workspace)
	}

	resolve(cl: number, clobber: boolean) {
		return this.p4.resolve(this.user, this.workspace, cl, clobber)
	}

	changes(limit: number, pending?: boolean) {
		return this.p4.changes(this.workspace, this.stream, limit, pending)
	}

	print(file: string) {
		return this.p4.print(this.workspace, Path.join(this.root, file))
	}

	unshelve(cl: number) {
		return this.p4.unshelve(this.user, this.workspace, cl)
	}

	deleteShelved(cl: number) {
		return this.p4.deleteShelved(this.user, this.workspace, cl)
	}
}

export const BRANCHES_FILE = 'functionaltest.branchmap.json'

export type StreamType = 'mainline' | 'development' | 'release'

export type Stream = {
	name: string
	streamType: StreamType
	owner?: string
	parent?: string
	depotName?: string
	options?: string
	paths?: string[]
	ignored?: string[]
}

type RobomergeBranchOptions = {
	//////////////
	// BranchBase
	rootPath: string
	isDefaultBot: boolean
	emailOnBlockage: boolean // if present, completely overrides BotConfig
	maxFilesPerIntegration: number // otherwise auto pause

	notify: string[]
	flowsTo: string[]
	forceFlowTo: string[]
	defaultFlow: string[]
	whitelist: string[]
	resolver: string | null
	aliases: string[]
	badgeProject: string | null

	pathsToMonitor: string[] | null


	////////////////////
	// NodeOptionFields
	disabled: boolean
	integrationMethod: string
	forceAll: boolean
	visibility: string[] | string
	blockAssetFlow: string[]

	streamDepot: string
	streamName: string
	streamSubpath: string
	workspace: (string | null)

	// if set, still generate workspace but use this name
	workspaceNameOverride: string
	additionalSlackChannelForBlockages: string
	ignoreBranchspecs: boolean
	lastGoodCLPath: string

	initialCL: number
	forcePause: boolean

	disallowSkip: boolean
	incognitoMode: boolean

	excludeAuthors: string[] // if present, completely overrides BotConfig

	p4MaxRowsOverride: number // use with care and check with p4 admins
}

export type RobomergeBranchSpec = Partial<RobomergeBranchOptions> & {
	name: string
}

// copied from branchdefs.ts
type EdgeOptionFields = {
	lastGoodCLPath: string
	additionalSlackChannel: string
	initialCL: number
	p4MaxRowsOverride: number // use with care and check with p4 admins

	disallowSkip: boolean
	incognitoMode: boolean

	excludeAuthors: string[]
}

export type EdgeProperties = Partial<EdgeOptionFields> & {
	from: string
	to: string
}

export abstract class FunctionalTest {
	readonly testName: string
	botName: string
	
	constructor(protected p4: Perforce, overrideTestName?: string) {
		this.testName = overrideTestName || this.constructor.name
	}

	static readonly dfltStreamOpts = {
		owner: 'root',
		parent: 'none',
		options: 'allsubmit unlocked notoparent nofromparent mergedown',
		paths: ['\n\tshare ...']
	}

	abstract async setup(): Promise<void>;

	abstract run(): Promise<any>;
	abstract getBranches(): RobomergeBranchSpec[];
	getEdges(): EdgeProperties[] { return [] }
	abstract verify(): Promise<any>;

	workspaceName(user: string, name: string) {
		return [user, this.testName, name].join('_')
	}

	getStreamPath(stream: Stream): string;
	getStreamPath(name: string, depotName?: string): string;
	getStreamPath(arg0: Stream | string, depotName?: string): string {
		let name: string = typeof(arg0) === 'object' ? arg0.name : arg0
		let depot: string = (typeof(arg0) === 'object' ? arg0.depotName : depotName) || this.testName
		return `//${depot}/${name}`
	}

	protected fullBranchName(branch: string) {
		return this.testName + P4Util.escapeBranchName(branch)
	}

	streamSpec(stream: Stream): string;
	streamSpec(namearg: string, streamType: StreamType, parent?: string, depotName?: string): string;
	streamSpec(arg0: Stream | string, streamType?: StreamType, parent?: string, depotName?: string) {
		const stream: Stream = 
			(typeof(arg0) === 'object') ? arg0 : 
			{
				name: arg0,
				streamType: streamType!,
				parent,
				depotName,
			}
		
		const spec = P4Util.specFormat(
			['Stream', this.getStreamPath(stream.name, stream.depotName)],
			['Owner', stream.owner || FunctionalTest.dfltStreamOpts.owner],
			['Name', stream.name],
			['Parent', stream.parent ? this.getStreamPath(stream.parent, stream.depotName) : FunctionalTest.dfltStreamOpts.parent],
			['Type', stream.streamType],
			['Description', '\n\tCreated by root.'],
			// development streams given fromparent - let's see if it matters
			['Options', stream.options || FunctionalTest.dfltStreamOpts.options],
			['Paths', [...FunctionalTest.dfltStreamOpts.paths, ...stream.paths || []].join('\n\t')],
			['Ignored', [...stream.ignored || [""]].join('\n\t')]
		)
		if (VERBOSE) console.log(spec)
		return spec
	}

	depotSpec(name?: string) {
		name = name || this.testName
		return P4Util.specFormat(
			["Depot", name],
			["Owner", "root"],
			["Description", "\n\tCreated by root."],
			["Type", "stream"],
			["StreamDepth", `//${name}/1`],
			["Map", name + '/...']
		)
	}

	protected makeBranchDef(stream: string, to: string[], forceAll?: boolean): RobomergeBranchSpec {
		return {
			streamDepot: this.testName,
			name: this.fullBranchName(stream),
			streamName: stream,
			flowsTo: to.map(str => this.fullBranchName(str)),
			forceAll: !!forceAll
		}
	}

	protected makeForceAllBranchDef(stream: string, to: string[]/*, additionalProps?: {[prop: string]: string}[]*/): RobomergeBranchSpec {
		return this.makeBranchDef(stream, to, true)
	}

	p4Client(user: string, stream: string, depotName?: string) {
		const ws = [user, this.testName, ...(depotName ? [depotName] : []), stream].join('_')
		return this.addClient(new P4Client(this.p4, user, ws, WORKSPACES_ROOT + ws, this.getStreamPath(stream, depotName), stream))
	}

	error(...err: string[]) {
		this.log(err, 'error')
	}

	warn(...warn: string[]) {
		this.log(warn, 'warn')
	}

	info(...info: string[]) {
		this.log(info, 'info')
	}

	debug(...debug: string[]) {
		this.log(debug, 'debug')
	}

	verbose(...verbose: string[]) {
		this.log(verbose, 'verbose')
	}

	private log(msg: string[], level: string) {
		console.log((this.testName + ':').padEnd(32) + `${colors[level](msg.join('\n'))}`)
	}

	async getBranchState(stream: string) {
		return new BranchState(await FunctionalTest.getBranchState(this.botName, this.fullBranchName(stream)))
	}

	async getEdgeState(sourceStream: string, targetStream: string) {
		return (await this.getBranchState(sourceStream)).getEdgeState(this.fullBranchName(targetStream))
	}

	static async getBranchState(botName: string, nodeName: string): Promise<any> {

		nodeName = nodeName.toUpperCase()
		const url = BRANCH_INFO_URL_TEMPLATE
			.replace('<bot>', botName)
			.replace('<branch>', nodeName.toUpperCase())

		const errorPrefix = "test failure: couldn't query branch info: "

		let json: { [key: string]: any }

		try {
			json = await getJson(url)
		}
		catch (err) {
			throw new Error(err.toString() + ': ' + (await err.responseBody))
		}
		// if (!body || body.toString().startsWith("No branch found")) {
		// 	throw new Error(errorPrefix + `no branch found for FUNCTIONALTEST:${nodeName}`)
		// }

		// if (body.toUpperCase() === "RESOURCE NOT FOUND") {
		// 	throw new Error(errorPrefix + `URL ${url} returned a "Resource Not Found" error.`)
		// }

		// const json = JSON.parse(body)

		if (!json.branch) {
			throw new Error(errorPrefix + `no branch found for ${botName}:${nodeName}`)
		}

		if (!json.branch.edges) {
			throw new Error(errorPrefix + `no edges found for ${botName}:${nodeName}`)
		}

		return json.branch
	}

	async ensureBlocked(sourceStream: string, targetStream?: string) {
		if (targetStream) {
			const edgeDisplayName = `${sourceStream} -> ${targetStream}`
			this.info(`Ensuring ${edgeDisplayName} is blocked`)
			const edgeState = await this.getEdgeState(sourceStream, targetStream)
			if (!edgeState.isBlocked()) {
				throw new Error(`${edgeDisplayName} isn't blocked!`)
			}
		}
		else {
			this.info(`Ensuring ${sourceStream} is blocked`)
			const branchState = await this.getBranchState(sourceStream)
			if (!branchState.isBlocked()) {
				throw new Error(`${sourceStream} isn't blocked!`)
			}
		}
	}

	async ensureNotBlocked(sourceStream: string, targetStream?: string) {
		const branchState = await this.getBranchState(sourceStream)
		if (targetStream) {
			const edgeDisplayName = `${sourceStream} -> ${targetStream}`
			this.info(`Ensuring ${edgeDisplayName} is not blocked`)
			if (branchState.isBlocked()) {
				throw new Error(`${sourceStream} (node) is blocked!`)
			}

			const edgeState = branchState.getEdgeState(this.fullBranchName(targetStream))
			if (edgeState.isBlocked()) {
				throw new Error(`${edgeDisplayName} is blocked!`)
			}
		}
		else {
			this.info(`Ensuring ${sourceStream} isn't blocked`)
			if (branchState.isBlocked()) {
				throw new Error(`${sourceStream} (node) is blocked!`)
			}
		}
	}

	/**
	 * Map of users (testuser1, testuser2, etc.) and their maps <stream: P4Client>
	 */
	private clients = new Map<string, Map<string, P4Client>>()
	private nodes: string[] = []
	private edges: [string, string][] = []

	getClient(stream: string, username = 'testuser1', depotName?: string) {
		if (this.clients.size === 0) {
			throw new Error(`No clients set up for ${this.testName}`)
		}
		const allUserClients = this.clients.get(username)
		if (!allUserClients) {
			throw new Error(`${username} has no clients set up in client mappings`)
		}
		const key = this.getStreamPath(stream, depotName)
		const client = allUserClients.get(key)

		//console.log(`${this.testName}: Retrieving ${username}:${key}`)
		if (!client) {
			throw new Error(`${username} has no client for stream ${key}`)
		}
		return client
	}

	private addClient(client: P4Client): P4Client {
		let clientMap = this.clients.get(client.user)
		if (!clientMap) {
			clientMap = new Map<string, P4Client>();
		}
		//const key = this.getStreamPath(client.stream)
		const key = client.stream
		if (clientMap.has(key)) {
			throw new Error(`Client list for ${client.user} already contains entry for stream ${key}`)
		}
		clientMap.set(key, client)
		//console.log(`${this.testName}: Adding ${client.user}:${key}:Client`)
		this.clients.set(client.user, clientMap)
		return client
	}

	async createStreamsAndWorkspaces(streams: Stream[], depotName?: string, users = ['testuser1']) {
		for (const stream of streams) {
			await this.p4.stream(this.streamSpec(stream))
		}

		const workspaceCreationPromises: Promise<void>[] = []
		users.forEach((username) => {
			for (const stream of streams) {
				const client = this.p4Client(username, stream.name, depotName)
				workspaceCreationPromises.push(client.create(P4Util.specForClient(client)))
			}
		})

		await Promise.all(workspaceCreationPromises)
	}

	// for now, all added branches assumed to come from same source branch
	async addTargetBranches(branches: RobomergeBranchSpec[], from: string, edgeProps?: EdgeProperties[]) {
		const rootClient = await getRootDataClient(this.p4, 'RoboMergeData_BranchMaps')

		const branchSpecFilename = this.botName.toLowerCase() + '.branchmap.json'

		// note only one test can safely use this unless we add some kind of change queue
		const botFileContentJson = await P4Util.readFile(rootClient, branchSpecFilename)
		const botFileContent = JSON.parse(botFileContentJson)
		const fromFullName = this.fullBranchName(from)
		for (const branchInBot of botFileContent.branches) {
			if (branchInBot.name === fromFullName) {
				branchInBot.flowsTo = [
					...(branchInBot.flowsTo || []),
					...branches.map(b => b.name)
				]
			}
		}

		botFileContent.branches = [...botFileContent.branches, ...branches]

		if (edgeProps) {
			botFileContent.edges = [...(botFileContent.edges || []), ...edgeProps]
		}
		P4Util.editFileAndSubmit(rootClient, branchSpecFilename, JSON.stringify(botFileContent), 'added branch')
	}

	async checkHeadRevision(stream: string, filename: string, expectedRev: number, depotName?: string) {
		const depotFileStr = `${this.getStreamPath(stream, depotName)}/${filename}`
		this.info(`Ensuring ${depotFileStr} has exactly ${expectedRev} revisions`)

		// Check for non-existant file request -- requires different use of p4.fstat
		let statResult: string
		if (expectedRev <= 0) {
			try {
				statResult = await this.p4.fstat(depotFileStr)
			}
			catch([err, _stdout]) {
				statResult = err.toString()
			}

			if (statResult.indexOf('no such file(s).') < 0) {
				throw new Error(`Expected no revision of ${depotFileStr}. Perforce returned:\n${statResult}`)
			}
		}
		else {
			statResult = await this.p4.fstat(depotFileStr)
			let headRev = parseInt(statResult.split(' ')[2])
			if (headRev !== expectedRev) {
				throw new Error(`Unexpected head revision of ${depotFileStr}. (Expected: ${expectedRev}, actual: ${headRev})`)
			}
		}
	}

	async verifyStompRequest(source: string, target: string, edgeState: EdgeState) {
		if (!edgeState.isBlocked()) {
			throw new Error('edge must be blocked to stomp!')
		}

		const conflictCl = edgeState.conflictCl
		const sourceBranchName = this.fullBranchName(source)
		const targetBranchName = this.fullBranchName(target)

		// verify stomp
		const endpoint = OPERATION_URL_TEMPLATE
			.replace('<bot>', this.botName)
			.replace('<node>', sourceBranchName)
			.replace('<op>', 'verifystomp')
		const url = `${endpoint}?cl=${conflictCl}&target=${targetBranchName}`
		let verifyResult: any

		try {
			const post = bent('POST', 'json', 200, 400)
			verifyResult = await post(url)
		}
		catch (err) {
			this.error(err)
			throw new Error(`Verifying Stomp with Url "${url}" returned an error.`)
		}

		return verifyResult
	}

	async performStompRequest(source: string, target: string, edgeState: EdgeState) {
		const conflictCl = edgeState.conflictCl
		const sourceBranchName = this.fullBranchName(source)
		const targetBranchName = this.fullBranchName(target)

		const stompEndpoint = OPERATION_URL_TEMPLATE
			.replace('<bot>', this.botName)
			.replace('<node>', sourceBranchName)
			.replace('<op>', 'stompchanges')
		const url = `${stompEndpoint}?cl=${conflictCl}&target=${targetBranchName}`

		// note tests are currently calling performStomp even if verifyRequest fails
		const post = bent('POST', 200, 400, 500)
		let response: bent.BentResponse
		try {
			response = await post(url) as bent.BentResponse
		}
		catch(err) {
			this.error(err)
			throw new Error(`Performing Stomp with Url "${url}" returned an error.`)
		}
		return {
			statusCode: response.statusCode,
			body: response
		}
	}

	async reconsider(source: string, cl: number, target?: string, commandOverride?: string) {
		const sourceBranchName = this.fullBranchName(source)

		let reconsiderEndpoint
		if (target) {
			reconsiderEndpoint = EDGE_OPERATION_URL_TEMPLATE.replace('<edge>', this.fullBranchName(target))
		}
		else {
			reconsiderEndpoint = OPERATION_URL_TEMPLATE	
		}

		const url = (reconsiderEndpoint
			.replace('<bot>', this.botName)
			.replace('<node>', sourceBranchName)
			.replace('<op>', 'reconsider')) + `?cl=${commandOverride ? encodeURIComponent(cl + ' ' + commandOverride) : cl}`;

		try {
			await bent('POST', 'string', 200)(url)
		}
		catch (err) {
			throw 'Reconsider error: ' + (await err.responseBody)
		}
	}

	wasMessagePostedToSlack(channel: string, cl: number) {
		return getJson(DUMMY_SLACK_DOMAIN + '/posted/' + channel)
			.then((cls: number[]) => cls.indexOf(cl) >= 0)
	}

	async verifyAndPerformStomp(source: string, target: string, additionalSlackChannel?: string) {
		const edgeState: EdgeState = await this.getEdgeState(source, target)

		const checkSlack = async () => {
			const slackChannel = this.botName.toLowerCase()
			const channelsStr = slackChannel + (additionalSlackChannel ? ' and ' + additionalSlackChannel : '')
			this.info(`Ensuring Slack message sent to ${channelsStr} for CL#${edgeState.conflictCl}`)

			const channels = [slackChannel]
			if (additionalSlackChannel) {
				channels.push(additionalSlackChannel)
			}

			for (const channel of channels) {
				if (!await this.wasMessagePostedToSlack(channel, edgeState.conflictCl)) {
					throw new Error(`No Slack message sent to ${channel} for CL#${edgeState.conflictCl}`)	
				}
			}
		}

		this.info(`Stomp ${source} -> ${target} @ CL#${edgeState.conflictCl}`)
		const [verifyResult] = await Promise.all([
			this.verifyStompRequest(source, target, edgeState),
			checkSlack()
		])

		if (!verifyResult.validRequest)
		{
			this.warn(
				verifyResult.validRequest, verifyResult.nonBinaryFilesResolved, verifyResult.remainingAllBinary,
				Array.isArray(verifyResult.files) ? verifyResult.files[0].targetFileName : `no files (${verifyResult.files})`)
			throw new Error('Stomp Verify returned unexpected values. Erroring...')
		}

		// attempt stomp
		const stompResult = await this.performStompRequest(source, target, edgeState)
		this.verbose('Waiting for RoboMerge to process Stomp')
		await this.waitForRobomergeIdle()

		return { verify: verifyResult, stomp: stompResult }
	}

	async createShelf(source: string, target: string) {
		const edgeState = await this.getEdgeState(source, target)
		if (!edgeState.isBlocked()) {
			this.error('Failed to create shelf')
			throw new Error('edge must be blocked to create shelf!')
		}

		const conflictCl = edgeState.conflictCl

		this.info(`Create shelf for ${source} -> ${target} @${conflictCl}`)

		const sourceBranchName = this.fullBranchName(source)
		const targetBranchName = this.fullBranchName(target)
		const targetClient = this.getClient(target)

		const endpoint = OPERATION_URL_TEMPLATE
			.replace('<bot>', this.botName)
			.replace('<node>', sourceBranchName)
			.replace('<op>', 'create_shelf')
		const response = await bent('POST', 'string')(`${endpoint}?cl=${conflictCl}&target=${targetBranchName}&workspace=${targetClient.workspace}`)

		if (response !== 'ok') {
			throw new Error('unexpected response from create_shelf: ' + response)
		}

		// wait for RoboMerge to process
		await this.waitForRobomergeIdle()

		return (await targetClient.changes(1, true))[0]!.change
	}

	storeNodesAndEdges() {
		for (const spec of this.getBranches()) {
			this.nodes.push(spec.name)
			if (spec.flowsTo) {
				for (const target of spec.flowsTo) {
					this.edges.push([spec.name, target])
				}
			}
		}
	}

	async isRobomergeIdle(dump?: boolean) {

		const latestP4CLs = new Map<string, Promise<number>>()
		for (const client of this.clients.get('testuser1')!.values()) {
			latestP4CLs.set(this.fullBranchName(client.name), client.changes(1).then(changes => changes[0].change))
		}

		const unblockedBranchStates = new Map<string, BranchState>()
		const branchStateErrors: string[] = []
		await Promise.all(this.nodes.map(async (node) => {
			let json
			try {
				json = await FunctionalTest.getBranchState(this.botName, node)
			}
			catch (err) {
				branchStateErrors.push(err.toString())
				return
			}
			const state = new BranchState(json)
			if (!state.isBlocked()) {
				unblockedBranchStates.set(state.name, state)
			}
		}))

		if (branchStateErrors.length > 0) {
			if (branchStateErrors[0].indexOf('no edges found') >= 0) {
				// temp
				this.warn(branchStateErrors[0])
				return false
			}

			throw new Error('Branch state errors:\n' + branchStateErrors.join('\n'))
			// if (true) {
			// 	this.warn()
			// 	console.log()
			// }
			// // this.warn('Assuming waiting for branchdefs to load')
			// return false
		}

		for (const [node, bs] of unblockedBranchStates) {
			const branchState = await bs

			const status = branchState.getStatusMessage()
			if (status) {
				if (dump) {
					this.verbose(`${node} status: ${status} (active: ${branchState.isActive()})`)
				}
				return false
			}

			const p4CL = await latestP4CLs.get(node)!
			const rmCL = branchState.getLastCL()

			const queue = branchState.getQueue()
			if (queue.length > 0) {
				if (dump) {
					console.log(`${node} not idle: `, queue)
				}
				return false
			}

			// not checking if node is paused, because we don't pause nodes in the tests
			if (rmCL < p4CL) {
				if (dump) {
					this.verbose(`RoboMerge is not idle: ${node} last CL ${rmCL} < ${p4CL}`)
				}
				return false
			}

			// console.log(`${node}: ${colors.magenta((await bs).getLastCL())} (${colors.cyan(await latestP4CLs.get(node)!)})`)
		}

		for (const [source, target] of this.edges) {
			const branchState = unblockedBranchStates.get(source)
			if (!branchState) {
				continue
			}

			const edgeState = branchState.getEdgeState(target)
			if (!edgeState.isBlocked()) {
				const p4CL = await latestP4CLs.get(source)!
				const rmCL = edgeState.getLastCL()
				if (rmCL < p4CL) {
					const gateCL = edgeState.getLastGoodCL()
					if (!gateCL || gateCL < 0 || rmCL < gateCL) {
						if (dump) {
							let msg = `RoboMerge is not idle: ${source} -> ${target} last CL ${rmCL} < ${p4CL}`
							if (gateCL) {
								msg += ` (gate: ${gateCL})`
							}
							this.verbose(msg)
						}
						return false
					}
				}
				// console.log(`${source} -> ${target}: ${colors.magenta(edgeState.getLastCL())}, ${p4CL} - ${edgeState.isBlocked()}`)
			}
		}
		return true
	}

	async waitForRobomergeIdle(dump = false, wait_limit = 15) {
		let sleepTime = .5
		for (let safety = 0; safety < wait_limit; ++safety) {
			if (await this.isRobomergeIdle(dump || safety === wait_limit - 1)) {
				return true
			}
			dump = false
			await System.sleep(sleepTime)
			sleepTime *= 1.2 // roughly 3 second interval after 10 tries, 20s after 20
		}
		this.warn('Time out!')
		throw new Error('RoboMerge never seemed to settle down')
	}
}

/**************************
 * PERFORCE HELPER COMMANDS
 **************************/
export class P4Util {
	static clientDefaults(): [string, string][] {
		return [
			['Description', 'Unit test workspace.'],
			['Options', 'noallwrite noclobber nocompress unlocked nomodtime normdir'],
			['SubmitOptions', 'submitunchanged'],
			['LineEnd', 'local']
		]
	}

	static specFormat(...items: [string, string][]) {
		return items.map(([k, v]) => `${k}: ${v}\n`).join('\n')
	}

	static clientSpec(workspace: string, user: string, path: string, stream: string) {
		return this.specFormat(
			...this.clientDefaults(),
			['Client', workspace],
			['Owner', user],
			['Root', path],
			['Stream', stream],
			['View', `\n\t${stream}/... //${workspace}/...`]
		)
	}

	static specForClient(client: P4Client) {
		return this.clientSpec(client.workspace, client.user, client.root, client.stream)
	}

	static async submit(client: P4Client, description: string) {
		const submitResult = await client.submit(description)
		const match = submitResult.match(/Change (\d+) submitted./) ||
			submitResult.match(/Change \d+ renamed change (\d+) and submitted./)
		return parseInt(match![1])
	}
	
	// put file ops in a static class?
	static addFile(client: P4Client, name: string, content: string, binary?: boolean) {
		return System.writeFile(Path.join(client.root, name), content)
			.then(() => client.add(name, binary))
	}
	
	static readFile(client: P4Client, name: string) {
		return System.readFile(Path.join(client.root, name))
	}

	static editFile(client: P4Client, name: string, newContent: string) {
		return client.edit(name)
			.then(() => System.writeFile(Path.join(client.root, name), newContent))
	}

	static addFileAndSubmit(client: P4Client, name: string, content: string, binary?: boolean) {
		return this.addFile(client, name, content, binary)
			.then(() => this.submit(client, `Adding file ${name}`))
	}

	static editFileAndSubmit(client: P4Client, name: string, newContent: string, robomergeCommand?: string) {
		let commitMessage = `Edited file '${name}'`
		if (robomergeCommand) {
			commitMessage += '\n#robomerge ' + robomergeCommand
		}
		return this.editFile(client, name, newContent)
			.then(() => this.submit(client, commitMessage))
	}
	
	static escapeBranchName(stream: string) {
		return stream.replace(/[^A-Za-z0-9]/g, '_')
	}
	
}