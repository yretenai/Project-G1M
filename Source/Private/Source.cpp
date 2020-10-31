#include "../Public/Source.h"

const char* g_pPluginName = "ProjectG1M";
const char* g_pPluginDesc = "G1M Noesis plugin";

//Options
bool bMerge = true;
bool bMergeG1MOnly = true;
bool bG1TMergeG1MOnly = false;
bool bAdditive = false;
bool bColor = false;
bool bDisplayDriver = false;
bool bDisableNUNNodes = false;

template<bool bBigEndian>
bool CheckModel(BYTE* fileBuffer, int bufferLen, noeRAPI_t* rapi)
{
	uint8_t ed = *(uint8_t*)(fileBuffer);
	return !(bBigEndian ^(ed == 0x47 ? true : false));
}

template<bool bBigEndian>
bool CheckTexture(BYTE* fileBuffer, int bufferLen, noeRAPI_t* rapi)
{
	uint8_t ed = *(uint8_t*)(fileBuffer+1);
	return !(bBigEndian ^ (ed == 0x31 ? true : false));
}

template<bool bBigEndian>
bool LoadTexture(BYTE* fileBuffer, int bufferLen, CArrayList<noesisTex_t*>& noeTex, noeRAPI_t* rapi)
{
	G1T<bBigEndian>(fileBuffer, bufferLen, noeTex, rapi);
	rapi->Noesis_ProcessCommands("-texnorepfn"); //avoid renaming of the first texture
	return 1;
}

template<bool bBigEndian>
noesisModel_t* LoadModel(BYTE* fileBuffer, int bufferLen, int& numMdl, noeRAPI_t* rapi)
{
	//Bool to check what kind of merge
	bool bMergeSeveralInternals;
	//Framerate
	int framerate = 0;

	//File related containers
	//g1m
	std::vector<std::string> g1mPaths;
	std::vector<int> fileLengths;
	std::vector<BYTE*> fileBuffers;
	//g1t
	std::vector<std::string> g1tPaths;
	std::vector<int> g1tFileLengths;
	std::vector<BYTE*> g1tFileBuffers;
	std::vector<uint32_t> g1tTextureOffsets;
	//oid
	bool bAlreadyHasOid = false;
	std::vector<std::string> oidPaths;
	std::vector<int> oidFileLengths;
	std::vector<BYTE*> oidFileBuffers;
	//g1a
	std::vector<std::string> g1aPaths;
	std::vector<int> g1aFileLengths;
	std::vector<BYTE*> g1aFileBuffers;
	std::vector<std::string> g1aFileNames;
	//g2a
	std::vector<std::string> g2aPaths;
	std::vector<int> g2aFileLengths;
	std::vector<BYTE*> g2aFileBuffers;
	std::vector<std::string> g2aFileNames;

	//unpooled Buffers
	std::vector<void*> unpooledBufs;

	//Offsets to the relevant G1M subSections
	std::vector<size_t>G1MSOffsets;
	std::vector<size_t>G1MMOffsets;
	std::vector<size_t>G1MGOffsets;
	std::vector<size_t>NUNOOffsets;
	std::vector<size_t>NUNVOffsets;
	std::vector<size_t>NUNSOffsets;

	//Subsections data containers
	std::vector<G1MM<bBigEndian>>G1MMs;
	std::vector<G1MG<bBigEndian>>G1MGs;
	std::vector<G1MS<bBigEndian>>internalSkeletons;
	std::vector<G1MS<bBigEndian>>externalSkeletons;
	std::vector<G1MS<bBigEndian>*>G1MSPointers;
	std::map<uint32_t, uint32_t> globalToFinal;
	std::vector<NUNO<bBigEndian>>NUNOs;
	std::vector<int> NUNOFileIDs;
	std::vector<NUNV<bBigEndian>>NUNVs;
	std::vector<int> NUNVFileIDs;
	std::vector<NUNS<bBigEndian>>NUNSs;
	std::vector<int> NUNSFileIDs;

	//NUN maps
	std::map<uint32_t, std::vector<uint32_t>> fileIndexToNUNO1Map;
	std::map<uint32_t, std::vector<uint32_t>> fileIndexToNUNO3Map;
	std::map<uint32_t, std::vector<uint32_t>> fileIndexToNUNV1Map;

	//Meshes
	std::vector<mesh_t> driverMeshes;

	//Fixes
	bool bIsSkeletonOrigin = true;
	RichMat43 rootCoords;
	
	void* ctx = rapi->rpgCreateContext(); //Create context

	//Get all the g1m paths if the option was selected
	if (bMerge)
	{
		std::filesystem::path inFile = rapi->Noesis_GetInputName();
		//for (auto& p : std::filesystem::recursive_directory_iterator(inFile.parent_path()))
		for (auto& p : std::filesystem::directory_iterator(inFile.parent_path()))
		{
			if (p.path().extension() == ".g1m")
				g1mPaths.push_back(p.path().string());
			if (p.path().extension() == ".g1t" && !bMergeG1MOnly)
				g1tPaths.push_back(p.path().string());
			if (p.path().extension() == ".g1a" && !bMergeG1MOnly)
			{
				g1aPaths.push_back(p.path().string());
				g1aFileNames.push_back(p.path().stem().string());
			}
			if (p.path().extension() == ".g2a" && !bMergeG1MOnly)
			{
				g2aPaths.push_back(p.path().string());
				g2aFileNames.push_back(p.path().stem().string());
			}
			if ((p.path().extension() == ".oid" || has_suffix(p.path().filename().string(), "Oid.bin")) && !bAlreadyHasOid)
				oidPaths.push_back(p.path().string());
		}
	}
	else
	{
		fileBuffers.push_back(fileBuffer);
		fileLengths.push_back(bufferLen);

	}

	//Prepare buffers and get the lengths
	for (const auto& p : g1mPaths)
	{
		int length;
		fileBuffers.push_back((BYTE*)rapi->Noesis_ReadFile(p.c_str(), &length));
		fileLengths.push_back(length);
	}

	for (const auto& p : g1tPaths)
	{
		int length;
		g1tFileBuffers.push_back((BYTE*)rapi->Noesis_ReadFile(p.c_str(), &length));
		g1tFileLengths.push_back(length);
	}

	for (const auto& p : g1aPaths)
	{
		int length;
		g1aFileBuffers.push_back((BYTE*)rapi->Noesis_ReadFile(p.c_str(), &length));
		g1aFileLengths.push_back(length);
	}

	for (const auto& p : g2aPaths)
	{
		int length;
		g2aFileBuffers.push_back((BYTE*)rapi->Noesis_ReadFile(p.c_str(), &length));
		g2aFileLengths.push_back(length);
	}

	for (const auto& p : oidPaths)
	{
		int length;
		oidFileBuffers.push_back((BYTE*)rapi->Noesis_ReadFile(p.c_str(), &length));
		oidFileLengths.push_back(length);
	}

	//Get all the offsets to the relevant sections
	bool bHasParsedG1MS = false;
	int fileID = 0;
	for (const auto& fb : fileBuffers)
	{
		//Parse header
		G1MHeader<bBigEndian> g1mHeader = reinterpret_cast<G1MHeader<bBigEndian>*>(fb + 12);

		//Going through all the sections and adding the chunks
		bHasParsedG1MS = false;
		size_t chunkOffset = g1mHeader.firstChunkOffset;
		for (auto i = 0; i < g1mHeader.chunkCount; i++)
		{
			GResourceHeader<bBigEndian> header = reinterpret_cast<GResourceHeader<bBigEndian>*>(fb + chunkOffset);
			switch (header.magic)
			{
			case G1MM_MAGIC:
				G1MMOffsets.push_back(chunkOffset);
				break;
			case G1MS_MAGIC:
				if (!bHasParsedG1MS)
				{
					G1MSOffsets.push_back(chunkOffset);
					bHasParsedG1MS = true;
				}
				break;
			case G1MG_MAGIC:
				G1MGOffsets.push_back(chunkOffset);
				break;
			case NUNO_MAGIC:
				NUNOOffsets.push_back(chunkOffset);
				NUNOFileIDs.push_back(fileID);
				break;
			case NUNV_MAGIC:
				NUNVOffsets.push_back(chunkOffset);
				NUNVFileIDs.push_back(fileID);
				break;
			case NUNS_MAGIC:
				NUNSOffsets.push_back(chunkOffset);
				NUNSFileIDs.push_back(fileID);
				break;
			default:
				break;
			}
			chunkOffset += header.chunkSize;
		}
		fileID++;
	}

	//Split between internal and external skels
	std::vector<bool> isOffsetInternal;
	for (auto i = 0; i< G1MSOffsets.size(); i++)
	{
		G1MS<bBigEndian> temp = G1MS<bBigEndian>(fileBuffers[i], G1MSOffsets[i]);
		if (temp.bIsInternal)
		{
			internalSkeletons.push_back(std::move(temp));
			isOffsetInternal.push_back(true);
		}

		else
		{
			externalSkeletons.push_back(std::move(temp));
			isOffsetInternal.push_back(false);
		}				
	}

	auto count1 = 0, count2 = 0;
	for (auto i = 0; i < G1MSOffsets.size(); i++)
	{
		//Get the pointers to the relevant skels for jointMaps
		if (isOffsetInternal[i])
		{
			G1MSPointers.push_back(&internalSkeletons[count1]);
			count1++;
		}
		else
		{
			G1MSPointers.push_back(&externalSkeletons[count2]);
			count2++;
		}
	}

	switch (internalSkeletons.size())
	{
	case 0:
	case 1:
		bMergeSeveralInternals = false;
		break;
	default:
		bMergeSeveralInternals = true;
		break;
	}

	if (bG1TMergeG1MOnly && (!bMerge || bMergeG1MOnly))
	{
		int length;
		char path[MAX_NOESIS_PATH];
		BYTE* g1tbuf = nullptr;
		for (auto& skel : internalSkeletons)
		{
			g1tbuf = rapi->Noesis_LoadPairedFile(rapi->Noesis_PooledString(const_cast<char*>("Select G1T texture file")),
				rapi->Noesis_PooledString(const_cast<char*>(".g1t")), length, path);
			if (g1tbuf)
			{
				g1tFileBuffers.push_back(g1tbuf);
				g1tFileLengths.push_back(length);
			}
		}
	}

	//Start skeleton merging, get internal indices first
	std::set<uint16_t> globalIndices;
	for (auto& s : internalSkeletons)
	{
		for (const auto& [key, value] : s.globalIDToLocalID)
		{
			if (globalIndices.find(key) == globalIndices.end())
			{
				globalIndices.insert(key);
				s.jointLocalIndexToExtract.push_back(s.globalIDToLocalID[key]);
			}			
		}
	}

	//Add physics bones from external skeletons if needed. A bit tricky since overlapping global IDs (see Fatal Frame model). Rely on layer to do that
	std::map<uint32_t, std::vector<uint32_t>> globalIndexToLayers;
	for (auto& s : externalSkeletons)
	{
		std::vector<std::tuple<uint32_t, uint32_t, uint32_t>> toUpdateIDs;
		for (const auto& [key, value] : s.globalIDToLocalID)
		{
			if (globalIndices.find(key) == globalIndices.end())
			{
				globalIndices.insert(key);
				s.jointLocalIndexToExtract.push_back(s.globalIDToLocalID[key]);
				globalIndexToLayers[key].push_back(s.skeletonLayer);
			}
			else //see if that global index was here before, with another layer
			{
				auto itr = globalIndexToLayers.find(key);
				if (itr != globalIndexToLayers.end() && key !=0)
				{
					auto& vec = itr->second;
					if (std::find(vec.begin(),vec.end(),s.skeletonLayer) == vec.end()) //Is that global index already on the same layer ? If not, update with a new gID
					{
						vec.push_back(s.skeletonLayer);
						toUpdateIDs.push_back(std::tuple<uint32_t, uint32_t, uint32_t>(key, key + s.skeletonLayer * 1000, value));
						globalIndices.insert(key + s.skeletonLayer * 1000);
						s.jointLocalIndexToExtract.push_back(s.globalIDToLocalID[key]);
					}
				}
			}

		}

		for (auto& data : toUpdateIDs)
		{
			s.localIDToGlobalID[std::get<2>(data)] = std::get<1>(data);
			s.globalIDToLocalID[std::get<1>(data)] = std::get<2>(data);
			int result = s.globalIDToLocalID.erase(std::get<0>(data));
		}
	}

	uint32_t jointCount = globalIndices.size();
	
	//Parsing all NUN chunks and getting the final joint count number
	//NUNO
	for (auto i = 0; i < NUNOOffsets.size(); i++)
	{
		NUNOs.push_back(std::move(NUNO<bBigEndian>(fileBuffers[NUNOFileIDs[i]], NUNOOffsets[i])));
	}

	for (auto& nun : NUNOs)
	{
		for (auto& nun1 : nun.Nuno1s)
		{
			jointCount += nun1.controlPoints.size();
		}

		for (auto& nun3 : nun.Nuno3s)
		{
			jointCount += nun3.controlPoints.size();
		}
	}

	//NUNV
	for (auto i = 0; i < NUNVOffsets.size(); i++)
	{
		NUNVs.push_back(std::move(NUNV<bBigEndian>(fileBuffers[NUNVFileIDs[i]], NUNVOffsets[i])));
	}

	for (auto& nun : NUNVs)
	{
		for (auto& nun1 : nun.Nunv1s)
		{
			jointCount += nun1.controlPoints.size();
		}
	}

	//NUNS
	for (auto i = 0; i < NUNSOffsets.size(); i++)
	{
		NUNSs.push_back(std::move(NUNS<bBigEndian>(fileBuffers[NUNSFileIDs[i]], NUNSOffsets[i])));
	}

	for (auto& nun : NUNSs)
	{
		for (auto& nun1 : nun.Nuns1s)
		{
			jointCount += nun1.controlPoints.size();
		}
	}	

	//Building merged skeleton, with no duplicates and all NUN bones
	uint32_t jointIndex = 0;
	modelBone_t* joints = nullptr; 
	if (internalSkeletons.size() > 0) //Only build if we have a valid skeleton to work with
	{
		joints = rapi->Noesis_AllocBones(jointCount);

		//Internal skeletons
		for (auto& s : internalSkeletons)
		{
			for (const auto& idx : s.jointLocalIndexToExtract)
			{
				modelBone_t* joint = joints + jointIndex;
				joint->index = jointIndex;
				globalToFinal[s.localIDToGlobalID[idx]] = jointIndex;
				uint32_t parent = s.joints[idx].parent;
				if (parent == 0xFFFFFFFF)
				{
					joint->eData.parent = nullptr;
					if (s.joints[idx].position.v[0] + s.joints[idx].position.v[1] + s.joints[idx].position.v[2])
					{
						bIsSkeletonOrigin = false;
						rootCoords = RichMat43();
						g_mfn->Math_VecCopy(s.joints[idx].position.v, rootCoords.m.o);
					}
				}
				else
				{
					joint->eData.parent = joints + globalToFinal[s.localIDToGlobalID[parent]];
				}
				snprintf(joint->name, 128, "bone_%d", s.localIDToGlobalID[idx]);
				joint->mat = s.joints[idx].rotation.ToMat43().GetInverse().m;
				g_mfn->Math_VecCopy(s.joints[idx].position.v, joint->mat.o);
				jointIndex += 1;
			}
		}

		//External skeletons (physics bones)
		for (auto& s : externalSkeletons)
		{
			for (const auto& idx : s.jointLocalIndexToExtract)
			{
				modelBone_t* joint = joints + jointIndex;
				joint->index = jointIndex;
				globalToFinal[s.localIDToGlobalID[idx]] = jointIndex;
				uint32_t parent = s.joints[idx].parent;
				if (parent >> 31) //0x80000000 flag
				{
					joint->eData.parent = joints + globalToFinal[internalSkeletons[0].localIDToGlobalID[parent]];
				}
				else
				{
					joint->eData.parent = joints + globalToFinal[s.localIDToGlobalID[parent]];
				}
				snprintf(joint->name, 128, "phys_bone_%d", s.localIDToGlobalID[idx]);
				joint->mat = s.joints[idx].rotation.ToMat43().GetInverse().m;
				g_mfn->Math_VecCopy(s.joints[idx].position.v, joint->mat.o);
				jointIndex += 1;
			}
		}

		rapi->rpgMultiplyBones(joints, globalIndices.size());

		//NUNO chunks
		for (auto i= 0; i< NUNOFileIDs.size(); i++) //Keep a reference to the ID for the NUNMap
		{

			for (auto& nun1 : NUNOs[i].Nuno1s)
			{
				uint32_t jointStart = jointIndex;
				uint32_t nunParentJointID;
				if (nun1.parentID >> 31)
					nunParentJointID = globalToFinal[nun1.parentID ^ 0x80000000];
				else
					nunParentJointID = globalToFinal[nun1.parentID];
				fileIndexToNUNO1Map[NUNOFileIDs[i]].push_back(jointStart);

				//Prepare driverMeshes
				mesh_t dMesh;
				createDriverVertexBuffers(dMesh, nun1.controlPoints.size(), unpooledBufs, rapi);
				float* posB = (float*)dMesh.posBuffer.address;
				uint16_t* bIB = (uint16_t*)dMesh.blendIndicesBuffer.address;
				float* bWB = (float*)dMesh.blendWeightsBuffer.address;

				std::vector<RichVec3> polys;

				//Process CPs
				for (auto j = 0; j < nun1.controlPoints.size(); j++)
				{
					auto p = nun1.controlPoints[j].ToVec3();
					auto& link = nun1.influences[j];
					auto parentID = link.P3;

					modelBone_t* joint = joints + jointIndex;
					RichMat43 jointMatrix = RichQuat().ToMat43().GetInverse().m;
					if (parentID == 0xFFFFFFFF)
					{
						parentID = nunParentJointID;
						g_mfn->Math_VecCopy(p.v, jointMatrix.m.o);
					}
					else
					{
						parentID += jointStart;
						RichMat43 mat1 = RichMat43(joints[nunParentJointID].mat);
						RichMat43 mat2 = RichMat43(joints[parentID].mat);
						jointMatrix = mat1 * mat2.GetInverse();
						p = jointMatrix.TransformPoint(p);
						g_mfn->Math_VecCopy(p.v, jointMatrix.m.o);
					}
					RichMat43 mat3 = RichMat43(joints[parentID].mat);
					joint->mat = (jointMatrix * mat3).m;
					snprintf(joint->name, 128, "nuno1_p_%d_bone_%d", nunParentJointID, jointIndex);
					joint->index = jointIndex;
					joint->eData.parent = joints + parentID;

					//Driver mesh buffers
					RichMat43 mat4 = RichMat43(joints[jointIndex].mat);
					RichVec3 pos = mat4.TransformPoint(RichVec3());
					posB[3 * j] = pos[0];
					posB[3 * j + 1] = pos[1];
					posB[3 * j + 2] = pos[2];
					bIB[j] = jointIndex;
					bWB[j] = 1;

					if (link.P1 > 0 && link.P3 > 0)
						polys.push_back(RichVec3(j, int(link.P1), int(link.P3)));
					if (link.P2 > 0 && link.P4 > 0)
						polys.push_back(RichVec3(j, int(link.P2), int(link.P4)));

					jointIndex++;
				}

				//Driver mesh indices
				createDriverIndexBuffers(dMesh, polys, unpooledBufs, rapi);

				driverMeshes.push_back(dMesh);
			}

			for (auto& nun3 : NUNOs[i].Nuno3s)
			{
				uint32_t jointStart = jointIndex;
				uint32_t nunParentJointID;
				if (nun3.parentID >> 31)
					nunParentJointID = globalToFinal[nun3.parentID ^ 0x80000000];
				else
					nunParentJointID = globalToFinal[nun3.parentID];
				fileIndexToNUNO3Map[NUNOFileIDs[i]].push_back(jointStart);

				//Prepare driverMeshes
				mesh_t dMesh;
				createDriverVertexBuffers(dMesh, nun3.controlPoints.size(), unpooledBufs, rapi);
				float* posB = (float*)dMesh.posBuffer.address;
				uint16_t* bIB = (uint16_t*)dMesh.blendIndicesBuffer.address;
				float* bWB = (float*)dMesh.blendWeightsBuffer.address;

				std::vector<RichVec3> polys;

				//Process CPs
				for (auto j = 0; j < nun3.controlPoints.size(); j++)
				{
					auto p = nun3.controlPoints[j].ToVec3();
					auto& link = nun3.influences[j];
					auto parentID = link.P3;

					modelBone_t* joint = joints + jointIndex;
					RichMat43 jointMatrix = RichQuat().ToMat43().GetInverse().m;
					if (parentID == 0xFFFFFFFF)
					{
						parentID = nunParentJointID;
						g_mfn->Math_VecCopy(p.v, jointMatrix.m.o);
					}
					else
					{
						parentID += jointStart;
						RichMat43 mat1 = RichMat43(joints[nunParentJointID].mat);
						RichMat43 mat2 = RichMat43(joints[parentID].mat);
						jointMatrix = mat1 * mat2.GetInverse();
						p = jointMatrix.TransformPoint(p);
						g_mfn->Math_VecCopy(p.v, jointMatrix.m.o);
					}
					RichMat43 mat3 = RichMat43(joints[parentID].mat);
					joint->mat = (jointMatrix * mat3).m;
					snprintf(joint->name, 128, "nuno3_p_%d_bone_%d", nunParentJointID,jointIndex);
					joint->index = jointIndex;
					joint->eData.parent = joints + parentID;

					//Driver mesh buffers
					RichMat43 mat4 = RichMat43(joints[jointIndex].mat);
					RichVec3 pos = mat4.TransformPoint(RichVec3());
					posB[3 * j] = pos[0];
					posB[3 * j+1] = pos[1];
					posB[3 * j+2] = pos[2];
					bIB[j] = jointIndex;
					bWB[j] = 1;

					if (link.P1 > 0 && link.P3 > 0)
						polys.push_back(RichVec3(j, int(link.P1), int(link.P3)));
					if (link.P2 > 0 && link.P4 > 0)
						polys.push_back(RichVec3(j, int(link.P2), int(link.P4)));

					jointIndex++;
				}

				//Driver mesh indices
				createDriverIndexBuffers(dMesh, polys, unpooledBufs, rapi);

				driverMeshes.push_back(dMesh);
			}
		}

		//NUNV chunks
		for (auto i = 0; i < NUNVFileIDs.size(); i++) //Keep a reference to the ID for the NUNMap
		{

			for (auto& nun1 : NUNVs[i].Nunv1s)
			{
				uint32_t jointStart = jointIndex;
				uint32_t nunParentJointID;
				if (nun1.parentID >> 31)
					nunParentJointID = globalToFinal[nun1.parentID ^ 0x80000000];
				else
					nunParentJointID = globalToFinal[nun1.parentID];

				//Prepare driverMeshes
				mesh_t dMesh;
				createDriverVertexBuffers(dMesh, nun1.controlPoints.size(), unpooledBufs, rapi);
				float* posB = (float*)dMesh.posBuffer.address;
				uint16_t* bIB = (uint16_t*)dMesh.blendIndicesBuffer.address;
				float* bWB = (float*)dMesh.blendWeightsBuffer.address;

				std::vector<RichVec3> polys;

				//Process CPs
				fileIndexToNUNV1Map[NUNVFileIDs[i]].push_back(jointStart);
				for (auto j = 0; j < nun1.controlPoints.size(); j++)
				{
					auto p = nun1.controlPoints[j].ToVec3();
					auto& link = nun1.influences[j];
					auto parentID = link.P3;

					modelBone_t* joint = joints + jointIndex;
					RichMat43 jointMatrix = RichQuat().ToMat43().GetInverse().m;
					if (parentID == 0xFFFFFFFF)
					{
						parentID = nunParentJointID;
						g_mfn->Math_VecCopy(p.v, jointMatrix.m.o);
					}
					else
					{
						parentID += jointStart;
						RichMat43 mat1 = RichMat43(joints[nunParentJointID].mat);
						RichMat43 mat2 = RichMat43(joints[parentID].mat);
						jointMatrix = mat1 * mat2.GetInverse();
						p = jointMatrix.TransformPoint(p);
						g_mfn->Math_VecCopy(p.v, jointMatrix.m.o);
					}
					RichMat43 mat3 = RichMat43(joints[parentID].mat);
					joint->mat = (jointMatrix * mat3).m;
					snprintf(joint->name, 128, "nunv1_p_%d_bone_%d", nunParentJointID, jointIndex);
					joint->index = jointIndex;
					joint->eData.parent = joints + parentID;

					//Driver mesh buffers
					RichMat43 mat4 = RichMat43(joints[jointIndex].mat);
					RichVec3 pos = mat4.TransformPoint(RichVec3());
					posB[3 * j] = pos[0];
					posB[3 * j + 1] = pos[1];
					posB[3 * j + 2] = pos[2];
					bIB[j] = jointIndex;
					bWB[j] = 1;

					if (link.P1 > 0 && link.P3 > 0)
						polys.push_back(RichVec3(j, int(link.P1), int(link.P3)));
					if (link.P2 > 0 && link.P4 > 0)
						polys.push_back(RichVec3(j, int(link.P2), int(link.P4)));

					jointIndex++;
				}

				//Driver mesh indices
				createDriverIndexBuffers(dMesh, polys, unpooledBufs, rapi);

				driverMeshes.push_back(dMesh);
			}
		}

		//NUNS Chunks
		for (auto i = 0; i < NUNSFileIDs.size(); i++) //Keep a reference to the ID for the NUNMap
		{

			for (auto& nun1 : NUNSs[i].Nuns1s)
			{
				uint32_t jointStart = jointIndex;
				uint32_t nunParentJointID;
				if (nun1.parentID >> 31)
					nunParentJointID = globalToFinal[nun1.parentID ^ 0x80000000];
				else
					nunParentJointID = globalToFinal[nun1.parentID];

				//Prepare driverMeshes
				mesh_t dMesh;		
				createDriverVertexBuffers(dMesh, nun1.controlPoints.size(), unpooledBufs, rapi);
				float* posB = (float*)dMesh.posBuffer.address;
				uint16_t* bIB = (uint16_t*)dMesh.blendIndicesBuffer.address;
				float* bWB = (float*)dMesh.blendWeightsBuffer.address;

				std::vector<RichVec3> polys;

				//Process CPs
				for (auto j = 0; j < nun1.controlPoints.size(); j++)
				{
					auto p = nun1.controlPoints[j].ToVec3();
					auto& link = nun1.influences[j];
					auto parentID = link.P3;

					modelBone_t* joint = joints + jointIndex;
					RichMat43 jointMatrix = RichQuat().ToMat43().GetInverse().m;
					if (parentID == 0xFFFFFFFF)
					{
						parentID = nunParentJointID;
						g_mfn->Math_VecCopy(p.v, jointMatrix.m.o);
					}
					else
					{
						parentID += jointStart;
						RichMat43 mat1 = RichMat43(joints[nunParentJointID].mat);
						RichMat43 mat2 = RichMat43(joints[parentID].mat);
						jointMatrix = mat1 * mat2.GetInverse();
						p = jointMatrix.TransformPoint(p);
						g_mfn->Math_VecCopy(p.v, jointMatrix.m.o);
					}
					RichMat43 mat3 = RichMat43(joints[parentID].mat);
					joint->mat = (jointMatrix * mat3).m;
					snprintf(joint->name, 128, "nuns1_p_%d_bone_%d", nunParentJointID, jointIndex);
					joint->index = jointIndex;
					joint->eData.parent = joints + parentID;

					//Driver mesh buffers
					RichMat43 mat4 = RichMat43(joints[jointIndex].mat);
					RichVec3 pos = mat4.TransformPoint(RichVec3());
					posB[3 * j] = pos[0];
					posB[3 * j + 1] = pos[1];
					posB[3 * j + 2] = pos[2];
					bIB[j] = jointIndex;
					bWB[j] = 1;

					if (link.P1 > 0 && link.P3 > 0)
						polys.push_back(RichVec3(j, int(link.P1), int(link.P3)));
					if (link.P2 > 0 && link.P4 > 0)
						polys.push_back(RichVec3(j, int(link.P2), int(link.P4)));

					jointIndex++;
				}

				//Driver mesh indices
				createDriverIndexBuffers(dMesh, polys, unpooledBufs, rapi);

				driverMeshes.push_back(dMesh);
			}
		}
	}
	
	if (bDisableNUNNodes)
		jointIndex = globalIndices.size();

	//Skel names if relevant
	if (joints > 0)
	{
		for (auto i = 0; i < oidFileBuffers.size(); i++)
		{
			OID<bBigEndian>(oidFileBuffers[i], oidFileLengths[i], joints, globalToFinal);
		}
	}

	//Anims if relevant
	CArrayList<noesisAnim_t*> animList;
	if (joints >0)
	{
		//Some extractors don't name g2a files as .g2a but .g1a instead. Need to check header to determine what process to use.
		uint32_t gaMagic;
		for (auto i = 0; i < g2aFileBuffers.size(); i++)
		{
			gaMagic = *(uint32_t*)(g2aFileBuffers[i]);
			if (bBigEndian)
				LITTLE_BIG_SWAP(gaMagic);
			if(gaMagic == G2A_MAGIC)
				G2A<bBigEndian>(g2aFileBuffers[i], g2aFileLengths[i], g2aFileNames[i], joints, jointIndex, globalToFinal, animList, unpooledBufs, framerate, bAdditive, rapi);
			else
				G1A<bBigEndian>(g2aFileBuffers[i], g2aFileLengths[i], g2aFileNames[i], joints, jointIndex, globalToFinal, animList, unpooledBufs, framerate, bAdditive, rapi);
		}
		for (auto i = 0; i < g1aFileBuffers.size(); i++)
		{
			gaMagic = *(uint32_t*)(g1aFileBuffers[i]);
			if (bBigEndian)
				LITTLE_BIG_SWAP(gaMagic);
			if (gaMagic == G2A_MAGIC)
				G2A<bBigEndian>(g1aFileBuffers[i], g1aFileLengths[i], g1aFileNames[i], joints, jointIndex, globalToFinal, animList, unpooledBufs, framerate, bAdditive, rapi);
			else
				G1A<bBigEndian>(g1aFileBuffers[i], g1aFileLengths[i], g1aFileNames[i], joints, jointIndex, globalToFinal, animList, unpooledBufs, framerate, bAdditive, rapi);
		}
	}

	//Getting the geometry
	for (auto i = 0; i < G1MGOffsets.size(); i++)
	{
		G1MS<bBigEndian> *internalSkel = nullptr, *externalSkel = nullptr;
		if (internalSkeletons.size() > 0)
		{
			if (!G1MSPointers[i]->bIsInternal)
			{
				internalSkel = &internalSkeletons[0];
				externalSkel = G1MSPointers[i];
			}
			else
			{
				internalSkel = G1MSPointers[i];
			}
		}
		G1MGs.push_back(std::move(G1MG<bBigEndian>(fileBuffers[i], G1MGOffsets[i],internalSkel,externalSkel,&globalToFinal)));
	}

	//Getting the matrices
	for (auto i = 0; i < G1MMOffsets.size(); i++)
	{
		G1MMs.push_back(std::move(G1MM<bBigEndian>(fileBuffers[i], G1MMOffsets[i])));
	}

	//Grab the textures from the G1T files
	CArrayList<noesisTex_t*> textureList;
	CArrayList<noesisMaterial_t*> matList;
	for (auto i = 0; i < g1tFileBuffers.size(); i++)
	{	
		g1tTextureOffsets.push_back(textureList.Num());
		G1T<bBigEndian>(g1tFileBuffers[i], g1tFileLengths[i], textureList, rapi);		
	}

	//Processing the geometry data
	for (auto i = 0; i < G1MGs.size(); i++)
	{
		auto& g1mg = G1MGs[i];
		auto& g1mm = G1MMs[i];
		//Retrieve LOD and physics type from section 9 (MeshGroups)
		std::set<uint32_t> submeshesIndex; //Keep track of all the indices of submeshes from LOD0
		std::map<uint32_t,bool> bIsPhysType1; //NUN meshes. We could replace maps by vectors but we're not sure if indices are always ordered
		std::map<uint32_t,bool> bIsPhysType2; //"Danglies" (hair strands etc)
		std::map<uint32_t, int32_t> nunMapJointIndex;
		for (auto& mesh : g1mg.meshGroups[0].meshes)
		{
			for (auto& index : mesh.indices)
			{
				submeshesIndex.insert(index); //Filter all submeshes that need to be rendered
				bIsPhysType1[index] = mesh.meshType == 1;
				bIsPhysType2[index] = mesh.meshType == 2;
				if (bIsPhysType1[index] && joints) //Only NUNMeshes, avoid crashing on SOFT. Only if NUN nodes have been parsed
				{
					if (mesh.externalID >= 0 && mesh.externalID < 10000)
						nunMapJointIndex[index] = fileIndexToNUNO1Map[i][mesh.externalID];
					else if (mesh.externalID >= 10000 && mesh.externalID < 20000)
						nunMapJointIndex[index] = fileIndexToNUNV1Map[i][mesh.externalID % 10000];
					else if (mesh.externalID >= 20000 && mesh.externalID < 30000)
						nunMapJointIndex[index] = fileIndexToNUNO3Map[i][mesh.externalID % 10000];
				}
			}
		}
		rapi->rpgSetOption(RPGOPT_BIGENDIAN, bBigEndian);
		//All the information has been retrieved, rendering the submeshes
		int32_t previousVBIndex = -1; //used to avoid changing buffers
		for (auto smIdx : submeshesIndex)
		{
			auto& submesh = g1mg.submeshes[smIdx];

			//Build the jointMap if we have a skeleton
			int* jointMap = nullptr;
			if (joints)
			{
				G1MGJointPalette<bBigEndian>& jointPalette = g1mg.jointPalettes[submesh.bonePaletteIndex]; //get the corresponding palette  first
				jointMap = (int*)rapi->Noesis_PooledAlloc(sizeof(int) * jointPalette.jointMapBuilder.size()*3);
				for (auto j = 0; j < jointPalette.jointMapBuilder.size(); j++)
				{
					jointMap[3 * j] = jointPalette.jointMapBuilder[j];
				}
			}
			
			//submesh name
			char mesh_name[128];
			snprintf(mesh_name, 128, "model_%d_submesh_%d", i, smIdx);
			rapi->rpgSetName(mesh_name);

			//Material info
			if (g1tFileBuffers.size() > 0)
			{
				noesisMaterial_t* material = rapi->Noesis_GetMaterialList(1, true);
				bool bHasDiffuse = false, bHasNormal = false;
				uint32_t matOffset = 0;
				if (bMergeSeveralInternals && i < g1tTextureOffsets.size())
					matOffset = g1tTextureOffsets[i];
				for (G1MGTexture<bBigEndian>& textureInfo : g1mg.materials[submesh.materialIndex].g1mgTextures)
				{
					if (textureInfo.textureType == 1 && !bHasDiffuse)
					{
						material->texIdx = textureInfo.index + matOffset;
						bHasDiffuse = true;
						if (textureInfo.layer == 1)
							material->flags |= NMATFLAG_DIFFUSE_UV1;
					}
					else if (textureInfo.textureType == 3 && !bHasNormal)
					{
						material->normalTexIdx = textureInfo.index + matOffset;
						if (textureInfo.layer == 1)
							material->flags |= NMATFLAG_NORMAL_UV1;
					}
				}
				char mat_name[128];
				snprintf(mat_name, 128, "model_%d_mat_%d", i, smIdx);
				material->name = rapi->Noesis_PooledString(mat_name);
				matList.Append(material);
				rapi->rpgSetMaterial(mat_name);
			}

			//Same buffer used, we just use the previously set buffers to push the corresponding submesh, we need to update the jointMap too.
			if (previousVBIndex == submesh.vertexBufferIndex) 
			{
				if (jointMap)
					rapi->rpgSetBoneMap(jointMap);

				auto& ibuf = g1mg.indexBuffers[submesh.indexBufferIndex];
				switch (submesh.indexBufferPrimType)
				{
				case 3:
					rapi->rpgCommitTriangles(ibuf.bufferAdress + submesh.indexBufferOffset * ibuf.bitwidth, ibuf.dataType, submesh.indexCount, RPGEO_TRIANGLE, 1);
					break;
				case 4:
					rapi->rpgCommitTriangles(ibuf.bufferAdress + submesh.indexBufferOffset * ibuf.bitwidth, ibuf.dataType, submesh.indexCount, RPGEO_TRIANGLE_STRIP, 1);
					break;
				default:
					break;
				}
				continue;
			}
			previousVBIndex = submesh.vertexBufferIndex;
			rapi->rpgClearBufferBinds();
			
			//auto& vbuf = g1mg.vertexBuffers[submesh.vertexBufferIndex];
			auto& ibuf = g1mg.indexBuffers[submesh.indexBufferIndex];


			//useful for cloth type1
			BYTE* controlPointsWeightsSet1 = nullptr; //Used for "horizontal" alignment with CPs on the same line, kind of
			BYTE* controlPointsWeightsSet2 = nullptr; 
			BYTE* centerOfMassWeightsSet1 = nullptr; //Used for "vertical" alignment, to determine along the previously computed horizontal positions
			BYTE* centerOfMassWeightsSet2 = nullptr;

			BYTE* controlPointRelativeIndices1 = nullptr;
			EG1MGVADatatype cPIdx1Type = EG1MGVADatatype::VADataType_Dummy;
			BYTE* controlPointRelativeIndices2 = nullptr;
			EG1MGVADatatype cPIdx2Type = EG1MGVADatatype::VADataType_Dummy;
			BYTE* controlPointRelativeIndices3 = nullptr;
			EG1MGVADatatype cPIdx3Type = EG1MGVADatatype::VADataType_Dummy;
			BYTE* controlPointRelativeIndices4 = nullptr;
			EG1MGVADatatype cPIdx4Type = EG1MGVADatatype::VADataType_Dummy;

			BYTE* depthFromDriver = nullptr;
			int32_t phys1Stride = -1;
			int32_t phys1Count = -1;

			//useful for cloth type 2 (probably a cleaner way to reuse existing pointers but oh well)
			BYTE* posB = nullptr;
			BYTE* jointIdB = nullptr;
			EG1MGVADatatype jointIdBType = EG1MGVADatatype::VADataType_Dummy;
			BYTE* jointIdB2 = nullptr;
			EG1MGVADatatype jointIdB2Type = EG1MGVADatatype::VADataType_Dummy;
			BYTE* jointWB = nullptr;
			EG1MGVADatatype jointWBType = EG1MGVADatatype::VADataType_Dummy;
			BYTE* jointWB2 = nullptr;
			EG1MGVADatatype jointWB2Type = EG1MGVADatatype::VADataType_Dummy;
			int32_t jStride =-1;
			int32_t jVCount = -1;
			
			//going through the semantics, setting the buffers
			for (auto& attribute : g1mg.vertexAttributeSets[submesh.vertexBufferIndex].attributes)
			{
				auto& vbuf = g1mg.vertexBuffers[attribute.bufferID];
				switch (attribute.semantic)
				{

				case EG1MGVASemantic::Position:
					switch (attribute.dataType)
					{
					case EG1MGVADatatype::VADataType_Float_x3:
					case EG1MGVADatatype::VADataType_Float_x4:
						if (bIsPhysType1[smIdx])
						{
							controlPointsWeightsSet1 = vbuf.bufferAdress + attribute.offset;
							phys1Stride = vbuf.stride;
							phys1Count = vbuf.count;
						}
						else if (bIsPhysType2[smIdx])
						{
							posB = vbuf.bufferAdress + attribute.offset;
						}
						else if(bIsSkeletonOrigin)
							rapi->rpgBindPositionBuffer(vbuf.bufferAdress + attribute.offset, RPGEODATA_FLOAT, vbuf.stride);
						else
						{
							transformPosF<bBigEndian>(vbuf.bufferAdress + attribute.offset, vbuf.count, vbuf.stride, &rootCoords.m);
							rapi->rpgBindPositionBuffer(vbuf.bufferAdress + attribute.offset, RPGEODATA_FLOAT, vbuf.stride);
						}
						break;
					case EG1MGVADatatype::VADataType_HalfFloat_x4:
						if (bIsSkeletonOrigin)
							rapi->rpgBindPositionBuffer(vbuf.bufferAdress + attribute.offset, RPGEODATA_HALFFLOAT, vbuf.stride);
						else
						{
							BYTE* posB = (BYTE*)rapi->Noesis_UnpooledAlloc(sizeof(float) * 3 * vbuf.count);
							unpooledBufs.push_back(posB);
							transformPosHF<bBigEndian>(vbuf.bufferAdress + attribute.offset, posB, vbuf.count, vbuf.stride, &rootCoords.m);
							rapi->rpgBindPositionBuffer(posB, RPGEODATA_FLOAT, 12);
						}		
						break;
					default:
						break;
					}
					break;

				case EG1MGVASemantic::Normal:
					switch (attribute.dataType)
					{
					case EG1MGVADatatype::VADataType_Float_x3:
					case EG1MGVADatatype::VADataType_Float_x4:
						if (bIsPhysType1[smIdx])
						{
							depthFromDriver = vbuf.bufferAdress + attribute.offset;
						}
						else
						{
							rapi->rpgBindNormalBuffer(vbuf.bufferAdress + attribute.offset, RPGEODATA_FLOAT, vbuf.stride);
						}
						break;
					case EG1MGVADatatype::VADataType_HalfFloat_x4:
						rapi->rpgBindNormalBuffer(vbuf.bufferAdress + attribute.offset, RPGEODATA_HALFFLOAT, vbuf.stride);
						break;
					default:
						break;
					}
					break;

				case EG1MGVASemantic::UV:
					switch (attribute.dataType)
					{
					case EG1MGVADatatype::VADataType_Float_x2:
						if(attribute.layer == 0)
							rapi->rpgBindUV1Buffer(vbuf.bufferAdress + attribute.offset, RPGEODATA_FLOAT, vbuf.stride);
						else if (attribute.layer == 1)
							rapi->rpgBindUV2Buffer(vbuf.bufferAdress + attribute.offset, RPGEODATA_FLOAT, vbuf.stride);
						break;
					case EG1MGVADatatype::VADataType_HalfFloat_x2:
						if (attribute.layer == 0)
							rapi->rpgBindUV1Buffer(vbuf.bufferAdress + attribute.offset, RPGEODATA_HALFFLOAT, vbuf.stride);
						else if (attribute.layer == 1)
							rapi->rpgBindUV2Buffer(vbuf.bufferAdress + attribute.offset, RPGEODATA_HALFFLOAT, vbuf.stride);
						break;
					case EG1MGVADatatype::VADataType_HalfFloat_x4:					
						rapi->rpgBindUV1Buffer(vbuf.bufferAdress + attribute.offset, RPGEODATA_HALFFLOAT, vbuf.stride);
						rapi->rpgBindUV2Buffer(vbuf.bufferAdress + attribute.offset + 4, RPGEODATA_HALFFLOAT, vbuf.stride);
					case EG1MGVADatatype::VADataType_UByte_x4:
						controlPointRelativeIndices4 = vbuf.bufferAdress + attribute.offset;
						cPIdx4Type = EG1MGVADatatype::VADataType_UByte_x4;
						break;
					case EG1MGVADatatype::VADataType_UShort_x4:
						controlPointRelativeIndices4 = vbuf.bufferAdress + attribute.offset;
						cPIdx4Type = EG1MGVADatatype::VADataType_UShort_x4;
						break;
					default:
						break;
					}
					break;

				case EG1MGVASemantic::JointIndex:
					//We only cache the info for later processing here since we don't know how many joint layers there are
					jStride = g1mg.vertexBuffers[attribute.bufferID].stride;
					jVCount = vbuf.count;
					if (attribute.layer == 0)
					{
						jointIdB = vbuf.bufferAdress + attribute.offset;
						jointIdBType = attribute.dataType;
					}
					else if (attribute.layer == 1)
					{
						jointIdB2 = vbuf.bufferAdress + attribute.offset;
						jointIdB2Type = attribute.dataType;
					}
					if (bIsPhysType1[smIdx])
					{
						controlPointRelativeIndices1 = vbuf.bufferAdress + attribute.offset;
						cPIdx1Type = attribute.dataType;
					}
					break;

				case EG1MGVASemantic::JointWeight:
					//We only cache the info for later processing here since we don't know how many joint layers there are
					if (attribute.layer == 0)
					{
						jointWB = vbuf.bufferAdress + attribute.offset;
						jointWBType = attribute.dataType;
					}
					else if (attribute.layer == 1)
					{
						jointWB2 = vbuf.bufferAdress + attribute.offset;
						jointWB2Type = attribute.dataType;
					}
					if (bIsPhysType1[smIdx])
						centerOfMassWeightsSet1 = vbuf.bufferAdress + attribute.offset;
					break;

				case EG1MGVASemantic::Binormal:
					switch (attribute.dataType)
					{
					case EG1MGVADatatype::VADataType_Float_x3:
					case EG1MGVADatatype::VADataType_Float_x4:
						if (bIsPhysType1[smIdx])
						{
							controlPointsWeightsSet2 = vbuf.bufferAdress + attribute.offset;
						}
						break;
					default:
						break;
					}
					break;

				case EG1MGVASemantic::Color:
					switch (attribute.dataType)
					{
					case EG1MGVADatatype::VADataType_Float_x3:
					case EG1MGVADatatype::VADataType_Float_x4:
						if (bIsPhysType1[smIdx] && attribute.layer !=0)
						{
							centerOfMassWeightsSet2 = vbuf.bufferAdress + attribute.offset;
						}
						else if (bColor)
						{
							if (attribute.dataType == EG1MGVADatatype::VADataType_Float_x3)
							{
								BYTE* fBuffer = (BYTE*)rapi->Noesis_UnpooledAlloc(sizeof(float) * 4 * vbuf.count);
								unpooledBufs.push_back(fBuffer);
								genColor3F<bBigEndian>(vbuf.bufferAdress + attribute.offset, fBuffer, vbuf.count, vbuf.stride);
								rapi->rpgBindColorBuffer(fBuffer, RPGEODATA_FLOAT, 16, 4);
							}
							else
							{
								rapi->rpgBindColorBuffer(vbuf.bufferAdress + attribute.offset, RPGEODATA_FLOAT, vbuf.stride, 4);
							}
						}
						break;
					case EG1MGVADatatype::VADataType_HalfFloat_x4:
						if(bColor)
							rapi->rpgBindColorBuffer(vbuf.bufferAdress + attribute.offset, RPGEODATA_HALFFLOAT, vbuf.stride, 4);
						break;
					case EG1MGVADatatype::VADataType_NormUByte_x4:
						if (bColor)
							rapi->rpgBindColorBuffer(vbuf.bufferAdress + attribute.offset, RPGEODATA_UBYTE, vbuf.stride, 4);
						break;
					default:
						break;
					}
					break;

				case EG1MGVASemantic::Fog:
					switch (attribute.dataType)
					{
					case EG1MGVADatatype::VADataType_UByte_x4:
						controlPointRelativeIndices3 = vbuf.bufferAdress + attribute.offset;
						cPIdx3Type = EG1MGVADatatype::VADataType_UByte_x4;
						break;
					case EG1MGVADatatype::VADataType_UShort_x4:
						controlPointRelativeIndices3 = vbuf.bufferAdress + attribute.offset;
						cPIdx3Type = EG1MGVADatatype::VADataType_UShort_x4;
						break;
					default:
						break;
					}
					break;

				case EG1MGVASemantic::PSize:
					switch (attribute.dataType)
					{
					case EG1MGVADatatype::VADataType_UByte_x4:
						controlPointRelativeIndices2 = vbuf.bufferAdress + attribute.offset;
						cPIdx2Type = EG1MGVADatatype::VADataType_UByte_x4;
						break;
					case EG1MGVADatatype::VADataType_UShort_x4:
						controlPointRelativeIndices2 = vbuf.bufferAdress + attribute.offset;
						cPIdx2Type = EG1MGVADatatype::VADataType_UShort_x4;
						break;
					default:
						break;
					}
					break;

				default:
					break;
				}
			}

			//Transforming vertices for Cloth Type 1
			if (bIsPhysType1[smIdx] && joints)
			{
				modelBone_t* CPSet = joints + nunMapJointIndex[smIdx];
				float* posB = (float*)(controlPointsWeightsSet1);
				RichVec3 u1, u2, u3, u4, v1, v2, v3, v4;
				RichVec4 centerOfMassWVec1, centerOfMassWVec2, controlPCMWVec1, controlPCMWVec2;
				//We're just going to rewrite the posBuffer then feed it.
				for (auto j = 0; j < phys1Count; j++)
				{
					uint32_t index = phys1Stride * j;
					
					//We want to use vectorization and use some matrix products to get the result directly
					controlPCMWVec1 = RichVec4((float*)(controlPointsWeightsSet1 + index));
					controlPCMWVec2 = RichVec4((float*)(controlPointsWeightsSet2 + index));
					centerOfMassWVec1 = RichVec4((float*)(centerOfMassWeightsSet1 + index));
					centerOfMassWVec2 = RichVec4((float*)(centerOfMassWeightsSet2 + index));

					if (bBigEndian)
					{
						controlPCMWVec1.ChangeEndian();
						controlPCMWVec2.ChangeEndian();
						centerOfMassWVec1.ChangeEndian();
						centerOfMassWVec2.ChangeEndian();
					}

					switch (cPIdx1Type)
					{
					case VADataType_UByte_x4:
					{
						uint8_t* indexPointer = (uint8_t*)controlPointRelativeIndices1;
						u1 = RichMat43(CPSet[indexPointer[index]].mat.o, CPSet[indexPointer[index + 1]].mat.o,
							CPSet[indexPointer[index + 2]].mat.o, CPSet[indexPointer[index + 3]].mat.o).GetTranspose().TransformVec4(controlPCMWVec1).ToVec3();
						v1 = RichMat43(CPSet[indexPointer[index]].mat.o, CPSet[indexPointer[index + 1]].mat.o,
							CPSet[indexPointer[index + 2]].mat.o, CPSet[indexPointer[index + 3]].mat.o).GetTranspose().TransformVec4(controlPCMWVec2).ToVec3();
						break;
					}
					case VADataType_UShort_x4:
					{
						uint16_t* indexPointer = (uint16_t*)(controlPointRelativeIndices1+index);
						u1 = RichMat43(CPSet[indexPointer[0]].mat.o, CPSet[indexPointer[1]].mat.o,
							CPSet[indexPointer[2]].mat.o, CPSet[indexPointer[3]].mat.o).GetTranspose().TransformVec4(controlPCMWVec1).ToVec3();
						v1 = RichMat43(CPSet[indexPointer[0]].mat.o, CPSet[indexPointer[1]].mat.o,
							CPSet[indexPointer[2]].mat.o, CPSet[indexPointer[3]].mat.o).GetTranspose().TransformVec4(controlPCMWVec2).ToVec3();
						break;
					}
					default:
						break;
					}

					switch (cPIdx2Type)
					{
					case VADataType_UByte_x4:
					{
						uint8_t* indexPointer = (uint8_t*)controlPointRelativeIndices2;
						u2 = RichMat43(CPSet[indexPointer[index]].mat.o, CPSet[indexPointer[index + 1]].mat.o,
							CPSet[indexPointer[index + 2]].mat.o, CPSet[indexPointer[index + 3]].mat.o).GetTranspose().TransformVec4(controlPCMWVec1).ToVec3();
						v2 = RichMat43(CPSet[indexPointer[index]].mat.o, CPSet[indexPointer[index + 1]].mat.o,
							CPSet[indexPointer[index + 2]].mat.o, CPSet[indexPointer[index + 3]].mat.o).GetTranspose().TransformVec4(controlPCMWVec2).ToVec3();
						break;
					}
					case VADataType_UShort_x4:
					{
						uint16_t* indexPointer = (uint16_t*)(controlPointRelativeIndices2+index);
						u2 = RichMat43(CPSet[indexPointer[0]].mat.o, CPSet[indexPointer[1]].mat.o,
							CPSet[indexPointer[2]].mat.o, CPSet[indexPointer[3]].mat.o).GetTranspose().TransformVec4(controlPCMWVec1).ToVec3();
						v2 = RichMat43(CPSet[indexPointer[0]].mat.o, CPSet[indexPointer[1]].mat.o,
							CPSet[indexPointer[2]].mat.o, CPSet[indexPointer[3]].mat.o).GetTranspose().TransformVec4(controlPCMWVec2).ToVec3();
						break;
					}
					default:
						break;
					}

					switch (cPIdx3Type)
					{
					case VADataType_UByte_x4:
					{
						uint8_t* indexPointer = (uint8_t*)controlPointRelativeIndices3;
						u3 = RichMat43(CPSet[indexPointer[index]].mat.o, CPSet[indexPointer[index + 1]].mat.o,
							CPSet[indexPointer[index + 2]].mat.o, CPSet[indexPointer[index + 3]].mat.o).GetTranspose().TransformVec4(controlPCMWVec1).ToVec3();
						v3 = RichMat43(CPSet[indexPointer[index]].mat.o, CPSet[indexPointer[index + 1]].mat.o,
							CPSet[indexPointer[index + 2]].mat.o, CPSet[indexPointer[index + 3]].mat.o).GetTranspose().TransformVec4(controlPCMWVec2).ToVec3();
						break;
					}
					case VADataType_UShort_x4:
					{
						uint16_t* indexPointer = (uint16_t*)(controlPointRelativeIndices3+index);
						u3 = RichMat43(CPSet[indexPointer[0]].mat.o, CPSet[indexPointer[1]].mat.o,
							CPSet[indexPointer[2]].mat.o, CPSet[indexPointer[3]].mat.o).GetTranspose().TransformVec4(controlPCMWVec1).ToVec3();
						v3 = RichMat43(CPSet[indexPointer[0]].mat.o, CPSet[indexPointer[1]].mat.o,
							CPSet[indexPointer[2]].mat.o, CPSet[indexPointer[3]].mat.o).GetTranspose().TransformVec4(controlPCMWVec2).ToVec3();
						break; 
					}
					default:
						break;
					}

					switch (cPIdx4Type)
					{
					case VADataType_UByte_x4:
					{
						uint8_t* indexPointer = (uint8_t*)controlPointRelativeIndices4;
						u4 = RichMat43(CPSet[indexPointer[index]].mat.o, CPSet[indexPointer[index + 1]].mat.o,
							CPSet[indexPointer[index + 2]].mat.o, CPSet[indexPointer[index + 3]].mat.o).GetTranspose().TransformVec4(controlPCMWVec1).ToVec3();
						v4 = RichMat43(CPSet[indexPointer[index]].mat.o, CPSet[indexPointer[index + 1]].mat.o,
							CPSet[indexPointer[index + 2]].mat.o, CPSet[indexPointer[index + 3]].mat.o).GetTranspose().TransformVec4(controlPCMWVec2).ToVec3();
						break;
					}
					case VADataType_UShort_x4:
					{
						uint16_t* indexPointer = (uint16_t*)(controlPointRelativeIndices4+index);
						u4 = RichMat43(CPSet[indexPointer[0]].mat.o, CPSet[indexPointer[1]].mat.o,
							CPSet[indexPointer[2]].mat.o, CPSet[indexPointer[3]].mat.o).GetTranspose().TransformVec4(controlPCMWVec1).ToVec3();
						v4 = RichMat43(CPSet[indexPointer[0]].mat.o, CPSet[indexPointer[1]].mat.o,
							CPSet[indexPointer[2]].mat.o, CPSet[indexPointer[3]].mat.o).GetTranspose().TransformVec4(controlPCMWVec2).ToVec3();
						break;
					}
					default:
						break;
					}

					RichVec3 a = RichMat43(u1, u2, u3, u4).GetTranspose().TransformVec4(centerOfMassWVec1).ToVec3();
					RichVec3 b = RichMat43(u1, u2, u3, u4).GetTranspose().TransformVec4(centerOfMassWVec2).ToVec3();
					RichVec3 c = RichMat43(v1, v2, v3, v4).GetTranspose().TransformVec4(centerOfMassWVec1).ToVec3();

					//The b and c cross product gives the direction that the point needs to be extruded in, from the driver shape.
					//The depth buffer gives the distance from the driver, a is the "base position".
					float depth = *(float*)(depthFromDriver + index + 12);
					float* debugDepth = (float*)(depthFromDriver + index);
					if (bBigEndian)
					{
						LITTLE_BIG_SWAP(depth);
					}
					if (c.Length() == 0) //Should probably check it at the start of the for loop for better performance. Oh well
						transformPosF<bBigEndian>(controlPointsWeightsSet1 + index, 1, phys1Stride, &CPSet->eData.parent->mat);
					else
					{
						RichVec3 d = b.Cross(c);
						c = d * depth + a;
						d = c.Normalized();
						if (bBigEndian)
						{
							c.ChangeEndian();
							d.ChangeEndian();
						}
						g_mfn->Math_VecCopy(c.v, (float*)(controlPointsWeightsSet1 + index));
						//g_mfn->Math_VecCopy(d.v, (float*)(depthFromDriver + index));
					}
				}
				rapi->rpgBindPositionBuffer(controlPointsWeightsSet1, RPGEODATA_FLOAT, phys1Stride);
				rapi->rpgBindNormalBuffer(depthFromDriver, RPGEODATA_FLOAT, phys1Stride);
			}

			//Transforming vertices for Cloth type 2
			if (bIsPhysType2[smIdx] && jStride>0 && joints)
			{				
				for (auto j = 0; j < submesh.vertexCount; j++)
				{
					uint32_t bPID;
					//Read the jointIndex that will give us the jointPalette entry with all its info
					switch (jointIdBType)
					{
					case EG1MGVADatatype::VADataType_UByte_x4:
						bPID = *(uint8_t*)(jointIdB + j * jStride);
						break;
					case EG1MGVADatatype::VADataType_UShort_x4:
						bPID = *(uint16_t*)(jointIdB + j * jStride);
						break;
					case EG1MGVADatatype::VADataType_UInt_x4:
						bPID = *(uint32_t*)(jointIdB + j * jStride);
						break;
					default:
						break;
					}
					
					bPID /= 3;
					uint32_t jID = g1mg.jointPalettes[submesh.bonePaletteIndex].entries[bPID].physicsIndex;;
					modelMatrix_t matrix = joints[jID].mat;
					//Here we assume that the first internal skel has the physics joint's parent (which is always the case on all the samples)
					transformPosF<bBigEndian>(posB + j*jStride, 1, jStride, &joints[jID].mat);
				}
				rapi->rpgBindPositionBuffer(posB, RPGEODATA_FLOAT, jStride);
			}

			//Skinning
			if (!bIsPhysType1[smIdx] && joints && jVCount >0)
			{
				int jidCount = jointIdB2 ? 8 : 4;
				//Annoying edge cases : 				
				//-only bone indices. Assign first weight to 1 and zero out the other components (done in skin function)
				//-boneIndex and weight first layers, boneIndex 2nd layer but no weight 2nd layer. Zero out the latter in that case (done in skin function)

				//Indices
				switch (jointIdBType)
				{
				case VADataType_UByte_x4:
					rapi->rpgBindBoneIndexBuffer(jointIdB, RPGEODATA_UBYTE, jStride, jidCount);
					break;
				case VADataType_UShort_x4:
					rapi->rpgBindBoneIndexBuffer(jointIdB, RPGEODATA_USHORT, jStride, jidCount);
					break;
				case VADataType_UInt_x4:
					rapi->rpgBindBoneIndexBuffer(jointIdB, RPGEODATA_UINT, jStride, jidCount);
					break;
				default:
					break;
				}				

				//Weights
				bool bHasWBeenSet = false;
				//See if we can inject the weights directly
				switch (jointWBType)
				{
				case VADataType_Float_x4:
					switch (jointWB2Type)
					{
					case VADataType_Float_x4:
						rapi->rpgBindBoneWeightBuffer(jointWB, RPGEODATA_FLOAT, jStride, jidCount);
						bHasWBeenSet = true;
						break;
					case VADataType_Dummy:
						if (!jointIdB2) //necessary to avoid edge case mentionned above
						{
							rapi->rpgBindBoneWeightBuffer(jointWB, RPGEODATA_FLOAT, jStride, jidCount);
							bHasWBeenSet = true;
						}
						break;
					default:
						break;
					}
					break;

				case VADataType_HalfFloat_x4:
					switch (jointWB2Type)
					{
					case VADataType_HalfFloat_x4:
						rapi->rpgBindBoneWeightBuffer(jointWB, RPGEODATA_HALFFLOAT, jStride, jidCount);
						bHasWBeenSet = true;
						break;
					case VADataType_Dummy:
						if (!jointIdB2) //necessary to avoid edge case mentionned above
						{
							rapi->rpgBindBoneWeightBuffer(jointWB, RPGEODATA_HALFFLOAT, jStride, jidCount);
							bHasWBeenSet = true;
						}
						break;
					default:
						break;
					}
					break;

				case VADataType_NormUByte_x4:
					switch (jointWB2Type)
					{
					case VADataType_NormUByte_x4:
						rapi->rpgBindBoneWeightBuffer(jointWB, RPGEODATA_UBYTE, jStride, jidCount);
						bHasWBeenSet = true;
						break;
					case VADataType_Dummy:
						if (!jointIdB2) //necessary to avoid edge case mentionned above
						{
							rapi->rpgBindBoneWeightBuffer(jointWB, RPGEODATA_UBYTE, jStride, jidCount);
							bHasWBeenSet = true;
						}
						break;
					default:
						break;
					}
					break;

				default:
					break;
				}

				if (!bHasWBeenSet) //Can't feed directly, need to build a custom buffer
				{
					BYTE* jointWBFinal = (BYTE*)rapi->Noesis_PooledAlloc(sizeof(float) * jidCount * jVCount);
					skinSMeshW<bBigEndian>(jointWB, jointWBType, jointWB2, jointWB2Type, jointWBFinal, jVCount, jStride, jointIdB2);
					rapi->rpgBindBoneWeightBuffer(jointWBFinal, RPGEODATA_FLOAT, jidCount * 4, jidCount);
				}
			}

			//Joint map if relevant
			if (jointMap)
				rapi->rpgSetBoneMap(jointMap);

			//Semantic buffers set, index buffer left
			{
				switch (submesh.indexBufferPrimType)
				{
				case 3:					
					rapi->rpgCommitTriangles(ibuf.bufferAdress + submesh.indexBufferOffset * ibuf.bitwidth, ibuf.dataType, submesh.indexCount, RPGEO_TRIANGLE, 1);
					//rapi->rpgCommitTriangles(nullptr, ibuf.dataType, submesh.vertexCount, RPGEO_POINTS, 1);
					break;
				case 4:
					rapi->rpgCommitTriangles(ibuf.bufferAdress + submesh.indexBufferOffset * ibuf.bitwidth, ibuf.dataType, submesh.indexCount, RPGEO_TRIANGLE_STRIP, 1);
					//rapi->rpgCommitTriangles(nullptr, ibuf.dataType, submesh.vertexCount, RPGEO_POINTS, 1);
					break;
				default:
					break;
				}
			}

		}
	}

	//Feeding driver meshes
	rapi->rpgSetOption(RPGOPT_BIGENDIAN, false);
	rapi->rpgClearBufferBinds();
	rapi->rpgSetBoneMap(nullptr);
	uint8_t dvmIndex = 0;
	if (!bDisableNUNNodes)
	{
		for (auto& dvM : driverMeshes)
		{
			noesisMaterial_t* material = rapi->Noesis_GetMaterialList(1, true);
			char mat_name[128];
			snprintf(mat_name, 128, "driver_%d_mat", dvmIndex);
			material->name = rapi->Noesis_PooledString(mat_name);
			material->texIdx = -1;
			material->diffuse[0] = material->diffuse[1] = material->diffuse[2] = material->diffuse[3] = 1;
			material->flags |= NMATFLAG_TWOSIDED;
			if (!bDisplayDriver)
				material->skipRender = true;
			matList.Append(material);
			rapi->rpgSetMaterial(mat_name);

			char mesh_name[128];
			snprintf(mesh_name, 128, "driver_%d", dvmIndex);
			rapi->rpgSetName(mesh_name);
			rapi->rpgBindPositionBuffer(dvM.posBuffer.address, dvM.posBuffer.dataType, dvM.posBuffer.stride);
			rapi->rpgBindBoneIndexBuffer(dvM.blendIndicesBuffer.address, dvM.blendIndicesBuffer.dataType, dvM.blendIndicesBuffer.stride, dvM.jointPerVertex);
			rapi->rpgBindBoneWeightBuffer(dvM.blendWeightsBuffer.address, dvM.blendWeightsBuffer.dataType, dvM.blendWeightsBuffer.stride, dvM.jointPerVertex);
			rapi->rpgCommitTriangles(dvM.indexBuffer.address, dvM.indexBuffer.dataType, dvM.indexBuffer.indexCount, dvM.indexBuffer.primType, true);
			dvmIndex += 1;
		}
	}

	//Joints and anims
	if (joints)
	{
		rapi->rpgSetExData_Bones(joints, jointIndex);
		int num = animList.Num();
		noesisAnim_t* anims = rapi->Noesis_AnimFromAnimsList(animList, num);
		if (anims)
			rapi->rpgSetExData_AnimsNum(anims, 1);
		if(!bDisableNUNNodes)
			rapi->rpgSetOption(RPGOPT_FILLINWEIGHTS, true);
	}
	//Materials
	noesisMatData_t* pMd = rapi->Noesis_GetMatDataFromLists(matList, textureList);
	if (pMd)
		rapi->rpgSetExData_Materials(pMd);

	noesisModel_t* mdl = rapi->rpgConstructModel();
	
	if (!mdl) 
	{
		//Bit of a dirty workaround if a model has bones but no geometry
		/*if (joints)
		{
			rapi->rpgSetExData_Bones(joints, jointIndex);
			BYTE* dummyPos = (BYTE*)rapi->Noesis_PooledAlloc(sizeof(float) * 6);
			float* dst = (float*)dummyPos;
			dst[0] = 0;
			dst[1] = 0;
			dst[2] = 0;
			dst[3] = internalSkeletons.back().joints.back().position.v[1];
			dst[4] = internalSkeletons.back().joints.back().position.v[0];
			dst[5] = internalSkeletons.back().joints.back().position.v[2];
			rapi->rpgBindPositionBuffer(dst, RPGEODATA_FLOAT, 12);
			rapi->rpgCommitTriangles(nullptr, RPGEODATA_USHORT, 2, RPGEO_POINTS, 0);
			mdl = rapi->rpgConstructModel();
			if (mdl)
				numMdl = 1;
		}*/
	}
	else
	{
		numMdl = 1;
	}

	//Freeing file buffers
	if (bMerge)
	{
		for (auto& f : fileBuffers)
		{
			rapi->Noesis_UnpooledFree(f);
			f = nullptr;
		}
		fileBuffers.clear();
	}

	for (auto& f : g1tFileBuffers)
	{
		rapi->Noesis_UnpooledFree(f);
		f = nullptr;
	}
	g1tFileBuffers.clear();

	for (auto& f : g1aFileBuffers)
	{
		rapi->Noesis_UnpooledFree(f);
		f = nullptr;
	}
	g1aFileBuffers.clear();

	for (auto& f : g2aFileBuffers)
	{
		rapi->Noesis_UnpooledFree(f);
		f = nullptr;
	}
	g2aFileBuffers.clear();

	for (auto& f : oidFileBuffers)
	{
		rapi->Noesis_UnpooledFree(f);
		f = nullptr;
	}
	oidFileBuffers.clear();	

	rapi->rpgDestroyContext(ctx);
	rapi->Noesis_ProcessCommands("-texnorepfn"); //avoid renaming of the first texture
	rapi->SetPreviewAnimSpeed(framerate);

	//Freeing buffers
	for (void* buf : unpooledBufs)
	{
		rapi->Noesis_UnpooledFree(buf);
		buf = nullptr;
	}
	unpooledBufs.clear();
	matList.Clear();
	textureList.Clear();
	animList.Clear();
	return mdl;
}

bool NPAPI_InitLocal(void)
{
	//Model handlers
	int fHandle = g_nfn->NPAPI_Register((char*)"G1M File (Little Endian)",(char*) ".g1m");
	if (fHandle < 0)
		return false;
	int fHandleBE = g_nfn->NPAPI_Register((char*)"G1M File (Big Endian)", (char*) ".g1m");
	if (fHandleBE < 0)
		return false;
	//Texture handlers
	int fTHandle = g_nfn->NPAPI_Register((char*)"G1T File (Little Endian)", (char*) ".g1t");
	if (fTHandle < 0)
		return false;
	int fTHandleBE = g_nfn->NPAPI_Register((char*)"G1T File (Big Endian)", (char*) ".g1t");
	if (fTHandleBE < 0)
		return false;


	//Options
	int optHandle;
	optHandle = g_nfn->NPAPI_RegisterTool(const_cast<char*>("Merge all assets in the same folder"), setMerge, nullptr);
	g_nfn->NPAPI_SetToolHelpText(optHandle, const_cast<char*>("Merges all the g1m, g1t, oid and g1a/g2a files."));
	g_nfn->NPAPI_SetToolSubMenuName(optHandle, const_cast<char*>("Project G1M"));
	getMerge(optHandle);

	optHandle = g_nfn->NPAPI_RegisterTool(const_cast<char*>("Only models when merging"), setMergeG1MOnly, nullptr);
	g_nfn->NPAPI_SetToolHelpText(optHandle, const_cast<char*>("If the merging option is set, only merge the g1m files and ignore the others."));
	g_nfn->NPAPI_SetToolSubMenuName(optHandle, const_cast<char*>("Project G1M"));
	getMergeG1MOnly(optHandle);

	optHandle = g_nfn->NPAPI_RegisterTool(const_cast<char*>("Select G1T when non/partial merge"), setG1TMergeG1MOnly, nullptr);
	g_nfn->NPAPI_SetToolHelpText(optHandle, const_cast<char*>("Select the G1T file manually."));
	g_nfn->NPAPI_SetToolSubMenuName(optHandle, const_cast<char*>("Project G1M"));
	getG1TMergeG1MOnly(optHandle);

	optHandle = g_nfn->NPAPI_RegisterTool(const_cast<char*>("Additive animations"), setAdditive, nullptr);
	g_nfn->NPAPI_SetToolHelpText(optHandle, const_cast<char*>("Set to true if the animations are additive."));
	g_nfn->NPAPI_SetToolSubMenuName(optHandle, const_cast<char*>("Project G1M"));
	getAdditive(optHandle);

	optHandle = g_nfn->NPAPI_RegisterTool(const_cast<char*>("Process vertex colors"), setColor, nullptr);
	g_nfn->NPAPI_SetToolHelpText(optHandle, const_cast<char*>("Extract the vertex colors."));
	g_nfn->NPAPI_SetToolSubMenuName(optHandle, const_cast<char*>("Project G1M"));
	getColor(optHandle);
	
	optHandle = g_nfn->NPAPI_RegisterTool(const_cast<char*>("Display physics drivers"), setDisplayDriver, nullptr);
	g_nfn->NPAPI_SetToolHelpText(optHandle, const_cast<char*>("Display the physics drivers."));
	g_nfn->NPAPI_SetToolSubMenuName(optHandle, const_cast<char*>("Project G1M"));
	getDisplayDriver(optHandle);

	optHandle = g_nfn->NPAPI_RegisterTool(const_cast<char*>("Disable NUN nodes"), setDisableNUNNodes, nullptr);
	g_nfn->NPAPI_SetToolHelpText(optHandle, const_cast<char*>("Only keep the base skeleton, ignoring the NUN nodes."));
	g_nfn->NPAPI_SetToolSubMenuName(optHandle, const_cast<char*>("Project G1M"));
	getDisableNUNNodes(optHandle);

	//Models
	g_nfn->NPAPI_SetTypeHandler_TypeCheck(fHandle, CheckModel<false>);
	g_nfn->NPAPI_SetTypeHandler_LoadModel(fHandle, LoadModel<false>);
	g_nfn->NPAPI_SetTypeHandler_TypeCheck(fHandleBE, CheckModel<true>);
	g_nfn->NPAPI_SetTypeHandler_LoadModel(fHandleBE, LoadModel<true>);

	//Textures
	g_nfn->NPAPI_SetTypeHandler_TypeCheck(fTHandle, CheckTexture<false>);
	g_nfn->NPAPI_SetTypeHandler_LoadRGBA(fTHandle, LoadTexture<false>);
	g_nfn->NPAPI_SetTypeHandler_TypeCheck(fTHandleBE, CheckTexture<true>);
	g_nfn->NPAPI_SetTypeHandler_LoadRGBA(fTHandleBE, LoadTexture<true>);
	
	if (!g_nfn->NPAPI_DebugLogIsOpen())
		g_nfn->NPAPI_PopupDebugLog(0);
	return true;
}

void NPAPI_ShutdownLocal(void) 
{

}