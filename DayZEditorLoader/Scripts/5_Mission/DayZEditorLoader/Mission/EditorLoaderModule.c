class EditorLoaderModule: JMModuleBase
{
	static bool ExportLootData = false;	
	
	protected bool m_Loaded = false;
	
	static ref map<int, ref OLinkT> WorldObjects;
	protected ref array<ref EditorWorldDataImport> m_WorldDataImports = {};
	
	static void LoadMapObjects()
	{
		WorldObjects = new map<int, ref OLinkT>();
		EditorLoaderLog("Loading World Objects into cache...");
				
		// Adds all map objects to the WorldObjects array
		ref array<Object> objects = {};
		ref array<CargoBase> cargos = {};
		GetGame().GetObjectsAtPosition(vector.Zero, 100000, objects, cargos);

		foreach (Object o: objects) {
			WorldObjects.Insert(o.GetID(), new OLinkT(o));
		}
		
		EditorLoaderLog(string.Format("Loaded %1 World Objects into cache", WorldObjects.Count()));
	}
	

	void EditorLoaderCreateBuilding(string type, vector position, vector orientation)
	{
		EditorLoaderLog(string.Format("Creating %1", type));
		// This will cause.... issues (Might remove in the future for Trader mod?)
		if (GetGame().IsKindOf(type, "Man") || GetGame().IsKindOf(type, "DZ_LightAI")) {
			return;
		}
		
	    Object obj = GetGame().CreateObjectEx(type, position, ECE_SETUP | ECE_UPDATEPATHGRAPH | ECE_CREATEPHYSICS);
		
		if (!obj) {
			return;
		}
		
	    obj.SetOrientation(orientation);
	    obj.SetFlags(EntityFlags.STATIC, false);
	    obj.Update();
		obj.SetAffectPathgraph(true, false);
		if (obj.CanAffectPathgraph()) { 
			GetGame().GetCallQueue(CALL_CATEGORY_SYSTEM).CallLater(GetGame().UpdatePathgraphRegionByObject, 100, false, obj);
		}
	}
	
	void EditorLoaderDeleteBuilding(int id, bool clear_cache = false)
	{
		if (!WorldObjects) {
			LoadMapObjects();
		}
		
		//EditorLoaderLog(string.Format("Deleting %1", id));
		
		OLinkT deleted_object = WorldObjects[id];
		if (deleted_object) {
			CF_ObjectManager.HideMapObject(deleted_object.Ptr());
		}
		
		if (clear_cache) {
			EditorLoaderLog("Clearing Cache...");
			delete WorldObjects;
		}
	}
	
	void EditorLoaderRemoteDeleteBuilding(CallType type, ref ParamsReadContext ctx, ref PlayerIdentity sender, ref Object target)
	{
		Param2<int, bool> delete_params;
		if (!ctx.Read(delete_params)) {
			return;
		}
		
		EditorLoaderDeleteBuilding(delete_params.param1, delete_params.param2);
	}

	override void OnMissionStart()
	{
		EditorLoaderLog("OnMissionStart");
				
		GetRPCManager().AddRPC("EditorLoaderModule", "EditorLoaderRemoteDeleteBuilding", this);

		// Everything below this line is the Server side syncronization :)
		if (!IsMissionHost()) return;
	
		if (!FileExist("$profile:/EditorFiles")) {
			EditorLoaderLog("EditorFiles directory not found, creating...");
			if (!MakeDirectory("$profile:/EditorFiles")) {
				EditorLoaderLog("Could not create EditorFiles directory. Exiting...");
				return;
			}
		}

		TStringArray files = {};
		string file_name;
		FileAttr file_attr;
		
		FindFileHandle find_handle = FindFile("$profile:/EditorFiles/*.dze", file_name, file_attr, FindFileFlags.ALL);
		files.Insert(file_name);
		
		while (FindNextFile(find_handle, file_name, file_attr)) {
			files.Insert(file_name);
		}
		
		CloseFindFile(find_handle);

		foreach (string file: files) {
			EditorLoaderLog("File found: " + file);
			EditorWorldDataImport data_import;
			JsonFileLoader<EditorWorldDataImport>.JsonLoadFile("$profile:/EditorFiles/" + file, data_import);
			
			if (data_import) {
				m_WorldDataImports.Insert(data_import);
				EditorLoaderLog("Loaded $profile:/EditorFiles/" + file);
			}
		}
		
		
		// Create and Delete buildings on Server Side
		foreach (EditorWorldDataImport editor_data: m_WorldDataImports) {
			
			EditorLoaderLog(string.Format("%1 created objects found", editor_data.EditorObjects.Count()));
			EditorLoaderLog(string.Format("%1 deleted objects found", editor_data.DeletedObjects.Count()));
			
			foreach (int deleted_object: editor_data.DeletedObjects) {
				EditorLoaderDeleteBuilding(deleted_object);
			}
			
			foreach (EditorObjectDataImport editor_object: editor_data.EditorObjects) {
				EditorLoaderCreateBuilding(editor_object.Type, editor_object.Position, editor_object.Orientation);
			}
		}

		// Maybe having a massive map this big is hurting clients :)
		// Server side only
		if (WorldObjects) {
			delete WorldObjects;	
		}
		
		// Runs thread that watches for EditorLoaderModule.ExportLootData = true;
		thread ExportLootData();
	}
	
	// Runs on both client AND server
	override void OnMissionFinish()
	{
		CF.ObjectManager.UnhideAllMapObjects(false);		
	}	
		
	private void SendClientData(PlayerBase player, PlayerIdentity identity)
	{
		// Delete buildings on client side
		for (int i = 0; i < m_WorldDataImports.Count(); i++) {
			for (int j = 0; j < m_WorldDataImports[i].DeletedObjects.Count(); j++) {
				// Signals that its the final deletion in the final file
				bool finished = (i == m_WorldDataImports.Count() - 1 && j == m_WorldDataImports[i].DeletedObjects.Count() - 1);
				GetRPCManager().SendRPC("EditorLoaderModule", "EditorLoaderRemoteDeleteBuilding", new Param2<int, bool>(m_WorldDataImports[i].DeletedObjects[j], finished), true, identity, player);
			}
		}
	}
	
	// When client connects to server, send the data to said client
	// This gets called on every restart, m_Loaded is a counter to that
	override void OnInvokeConnect(PlayerBase player, PlayerIdentity identity)
	{
		EditorLoaderLog("OnInvokeConnect");
				
		if (GetGame().IsServer() && !m_Loaded) {
			thread SendClientData(player, identity);
			m_Loaded = true;
		}
	}
	
	private void ExportLootData()
	{
		while (true) {
			if (GetCEApi() && ExportLootData) {
				GetCEApi().ExportProxyData(vector.Zero, 100000);
				return;
			}
			
			Sleep(1000);
		}
	}
	
	override bool IsClient() 
	{
		return true;
	}
	
	override bool IsServer()
	{
		return true;
	}
	
	static void EditorLoaderLog(string msg)
	{
		PrintFormat("[EditorLoader] %1", msg);
	}
}
