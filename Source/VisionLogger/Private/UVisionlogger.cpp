// Copyright 2018, Institute for Artificial Intelligence - University of Bremen

#include "UVisionlogger.h"
#include "ConstructorHelpers.h"
#include "Engine.h"
#include <algorithm>
#include <sstream>
#include <chrono>


// Sets default values
AUVisionlogger::AUVisionlogger()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	ColorsUsed = 0;
	Width = 480;
	Height = 300;
	FieldOfView = 90.0;
	FrameRate = 0;
	MongoIp = TEXT("127.0.0.1");
	MongoPort = 27017;
	MongoDBName = TEXT("VisionLogger");
	FDateTime now = FDateTime::UtcNow();
	MongoCollectionName = FString::FromInt(now.GetYear()) + "_" + FString::FromInt(now.GetMonth()) + "_" + FString::FromInt(now.GetDay())
							+ "_" + FString::FromInt(now.GetHour()) + "_" + FString::FromInt(now.GetMinute());

	bImageSameSize = false;
	bCaptureColorImage = false;
	bCaptureMaskImage = false;
	bCaptureDepthImage = false;
	bInitialAsyncTask = true;
	bColorFirsttick = true;
	bMaskFirsttick = true;
	bDepthFirsttick = true;
	bColorSave = false;
	bMaskSave = false;
	bDepthSave = false;

	ColorImgCaptureComp = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("ColorCapture"));
	ColorImgCaptureComp->SetupAttachment(RootComponent);
	ColorImgCaptureComp->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	ColorImgCaptureComp->TextureTarget = CreateDefaultSubobject<UTextureRenderTarget2D>(TEXT("ColorTarget"));
	ColorImgCaptureComp->TextureTarget->InitAutoFormat(Width, Height);
	ColorImgCaptureComp->FOVAngle = FieldOfView;

	// Create the vision capture components
	MaskImgCaptureComp = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("MaskCapture"));
	MaskImgCaptureComp->SetupAttachment(RootComponent);
	MaskImgCaptureComp->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	MaskImgCaptureComp->TextureTarget = CreateDefaultSubobject<UTextureRenderTarget2D>(TEXT("MaskTarget"));
	MaskImgCaptureComp->TextureTarget->InitAutoFormat(Width, Height);
	MaskImgCaptureComp->FOVAngle = FieldOfView;

	DepthImgCaptureComp = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("DepthCapture"));
	DepthImgCaptureComp->SetupAttachment(RootComponent);
	DepthImgCaptureComp->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	DepthImgCaptureComp->TextureTarget = CreateDefaultSubobject<UTextureRenderTarget2D>(TEXT("DepthTarget"));
	DepthImgCaptureComp->TextureTarget->InitAutoFormat(Width, Height);
	DepthImgCaptureComp->FOVAngle = FieldOfView;

	// Get depth scene material for postprocessing
	ConstructorHelpers::FObjectFinder<UMaterial> MaterialDepthFinder(TEXT("Material'/VisionLogger/SceneDepthWorldUnits.SceneDepthWorldUnits'"));
	if (MaterialDepthFinder.Object != nullptr)
	{
		//MaterialDepthInstance = UMaterialInstanceDynamic::Create(MaterialDepthFinder.Object, DepthImgCaptureComp);
		MaterialDepthInstance = (UMaterial*)MaterialDepthFinder.Object;
		if (MaterialDepthInstance != nullptr)
		{
			DepthImgCaptureComp->PostProcessSettings.AddBlendable(MaterialDepthInstance, 1);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Could not load material for depth."));
	}

	ColorImgCaptureComp->SetHiddenInGame(true);
	ColorImgCaptureComp->Deactivate();
	MaskImgCaptureComp->SetHiddenInGame(true);
	MaskImgCaptureComp->Deactivate();
	DepthImgCaptureComp->SetHiddenInGame(true);
	DepthImgCaptureComp->Deactivate();


	// Setting flags for each camera
	ShowFlagsVertexColor(MaskImgCaptureComp->ShowFlags);
	ShowFlagsPostProcess(DepthImgCaptureComp->ShowFlags);
	

}

// Called when the game starts or when spawned
void AUVisionlogger::BeginPlay()
{
	Super::BeginPlay();
	FTimerHandle TimerHandle;
	GetWorld()->GetTimerManager().SetTimer(TimerHandle, this, &AUVisionlogger::Initial, 1.0f , false);
	
}

// Called every frame
void AUVisionlogger::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	FVector Position = GetWorld()->GetFirstPlayerController()->PlayerCameraManager->GetCameraLocation();
	FRotator Rotation = GetWorld()->GetFirstPlayerController()->PlayerCameraManager->GetCameraRotation();
	ColorImgCaptureComp->SetWorldLocationAndRotation(Position, Rotation);
	MaskImgCaptureComp->SetWorldLocationAndRotation(Position, Rotation);
	DepthImgCaptureComp->SetWorldLocationAndRotation(Position, Rotation);

}

void AUVisionlogger::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
	//StopAsyncTask(ColorAsyncWorker);
}

void AUVisionlogger::Initial()
{
	if (bSaveAsImage)
	{
		static IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
		ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);
	}

	if (bImageSameSize)
	{
		ColorViewport = GetWorld()->GetGameViewport()->Viewport;
		Width = ColorViewport->GetRenderTargetTextureSizeXY().X;
		Height = ColorViewport->GetRenderTargetTextureSizeXY().Y;
		ColorImgCaptureComp->TextureTarget->InitAutoFormat(Width, Height);
		MaskImgCaptureComp->TextureTarget->InitAutoFormat(Width, Height);
		DepthImgCaptureComp->TextureTarget->InitAutoFormat(Width, Height);

	}
	ColorImgCaptureComp->TextureTarget->InitAutoFormat(Width, Height);
	MaskImgCaptureComp->TextureTarget->InitAutoFormat(Width, Height);
	DepthImgCaptureComp->TextureTarget->InitAutoFormat(Width, Height);
	// Initializing buffers for reading images from the GPU
	ColorImage.AddZeroed(Width*Height);
	MaskImage.AddZeroed(Width*Height);
	DepthImage.AddZeroed(Width*Height);
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::Printf(TEXT("Image Size: x: %i, y: %i"), Width, Height));

	if (bCaptureColorImage) {
		
		ColorImgCaptureComp->TextureTarget->TargetGamma = 1;	
		ColorImgCaptureComp->SetHiddenInGame(false);
		ColorImgCaptureComp->Activate();
	}


	if (bCaptureMaskImage)
	{
		if (ColorAllObjects()) {
			UE_LOG(LogTemp, Warning, TEXT("All the objects has colored"));
		}
		MaskImgCaptureComp->SetHiddenInGame(false);
		MaskImgCaptureComp->Activate();
	}

	if (bCaptureDepthImage)
	{
		DepthImgCaptureComp->SetHiddenInGame(false);
		DepthImgCaptureComp->Activate();
	}


	// Call the timer 
	SetFramerate(FrameRate);
}

void AUVisionlogger::SetFramerate(const float NewFramerate)
{
	if (NewFramerate > 0.0f)
	{
		// Update Camera on custom timer tick (does not guarantees the UpdateRate value,
		// since it will be eventually triggered from the game thread tick

		FTimerHandle TimerHandle;
		GetWorld()->GetTimerManager().SetTimer(TimerHandle, this, &AUVisionlogger::TimerTick, 1/NewFramerate, true);

	}
}

void AUVisionlogger::InitAsyncTask(FAsyncTask<RawDataAsyncWorker>* AsyncWorker, TArray<FColor>& image, FDateTime Stamp,FString Name,int Width,int Height)
{
			
	AsyncWorker = new FAsyncTask<RawDataAsyncWorker>(image, ImageWrapper, Stamp, Name, Width, Height);
	AsyncWorker->StartBackgroundTask();
	AsyncWorker->EnsureCompletion();
			

}

void AUVisionlogger::CurrentAsyncTask(FAsyncTask<RawDataAsyncWorker>* AsyncWorker)
{
	if (AsyncWorker->IsDone()|| bInitialAsyncTask)
	{	
		bInitialAsyncTask = false;
		UE_LOG(LogTemp, Warning, TEXT("Start writing Color Image"));
		AsyncWorker->StartBackgroundTask();
		AsyncWorker->EnsureCompletion();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[%s][%d] SKIP new task "), TEXT(__FUNCTION__), __LINE__);
	}

}

void AUVisionlogger::StopAsyncTask(FAsyncTask<RawDataAsyncWorker>* AsyncWorker)
{
	if (AsyncWorker)
	{
		// Wait for worker to complete before deleting it
		AsyncWorker->EnsureCompletion();
		delete AsyncWorker;
		AsyncWorker = nullptr;
	}
}

void AUVisionlogger::TimerTick()
{
	FDateTime Stamp = FDateTime::UtcNow();
	
	if (bCaptureColorImage)
	{		
		if (!bColorFirsttick && ColorPixelFence.IsFenceComplete()) {
			if (bSaveAsImage) {
				
				InitAsyncTask(ColorAsyncWorker, ColorImage, Stamp, TEXT("COLOR"), Width, Height);
				bColorSave = true;
			}
			StopAsyncTask(ColorAsyncWorker);
		}
		if (bColorFirsttick)
		{
			ProcessColorImg();
			bColorFirsttick = false;
		}
		else if (bColorSave)
		{
			ProcessColorImg();
			bColorSave = false;
		}	
		UE_LOG(LogTemp, Warning, TEXT("Read Color Image"));
	}

	if (bCaptureMaskImage)
	{
		if (!bMaskFirsttick && MaskPixelFence.IsFenceComplete()) {
			if (bSaveAsImage)
			{
				InitAsyncTask(MaskAsyncWorker, MaskImage, Stamp, TEXT("MASK"), Width, Height);
				bMaskSave = true;
			}
			StopAsyncTask(MaskAsyncWorker);
		}
		if (bMaskFirsttick)
		{
			ProcessMaskImg();
			bMaskFirsttick = false;
		}
		else if (bMaskSave)
		{
			ProcessMaskImg();
			bMaskSave = false;
		}
		UE_LOG(LogTemp, Warning, TEXT("Read Mask Image"));
		
	}

	if (bCaptureDepthImage)
	{
		if (!bDepthFirsttick && DepthPixelFence.IsFenceComplete())
		{
			if (bSaveAsImage)
			{
				InitAsyncTask(DepthAsyncWorker, DepthImage, Stamp, TEXT("DEPTH"), Width, Height);
				bDepthSave = true;
			}
			StopAsyncTask(DepthAsyncWorker);
		}
		if (bDepthFirsttick)
		{
			ProcessDepthImg();
			bDepthFirsttick = false;
		}
		else if (bDepthSave)
		{
			ProcessDepthImg();
			bMaskSave = false;
		}	
		UE_LOG(LogTemp, Warning, TEXT("Read Depth Image"));
		
	}
}

bool AUVisionlogger::ConnectMongo(FString & MongoIp, int & MongoPort, FString & MongoDBName, FString & MongoCollection)
{
	//FString Furi_str = TEXT("mongodb://") + MongoIp + TEXT(":") + FString::FromInt(MongoPort);
	//std::string uri_str(TCHAR_TO_UTF8(*Furi_str));
	//std::string database_name(TCHAR_TO_UTF8(*MongoDBName));
	//std::string collection_name(TCHAR_TO_UTF8(*MongoCollection));

	////Required to initialize libmongoc's internals
	//mongoc_init();

	////create a new client instance
	//client = mongoc_client_new(uri_str.c_str());

	////Register the application name so we can track it in the profile logs on the server
	//mongoc_client_set_appname(client, "Mongo_data");

	////Get a handle on the database and collection 
	//database = mongoc_client_get_database(client, database_name.c_str());
	//collection = mongoc_client_get_collection(client, database_name.c_str(), collection_name.c_str());

	////connect mongo database
	//if (client) {

	//	UE_LOG(LogTemp, Warning, TEXT("Mongo Database has been connected"));
	//	return true;
	//}
	//else {
	//	UE_LOG(LogTemp, Warning, TEXT("Database connection failed"));
	//	return false;
	//}
	return false;
}

void AUVisionlogger::ProcessColorImg()
{
	FReadSurfaceDataFlags ReadSurfaceDataFlags;
	ReadSurfaceDataFlags.SetLinearToGamma(false);
	//ColorViewport->Draw();
	//ReadPixels(ColorImgCaptureComp, ColorImage, ReadSurfaceDataFlags);	
	FTextureRenderTargetResource* ColorRenderResource = ColorImgCaptureComp->TextureTarget->GameThread_GetRenderTargetResource();
	ReadPixels(ColorRenderResource, ColorImage);
	ColorPixelFence.BeginFence();
}

void AUVisionlogger::ProcessMaskImg()
{
	FTextureRenderTargetResource* MaskRenderResource = MaskImgCaptureComp->TextureTarget->GameThread_GetRenderTargetResource();
	ReadPixels(MaskRenderResource, MaskImage);
	MaskPixelFence.BeginFence();
	
}

void AUVisionlogger::ProcessDepthImg()
{
	FTextureRenderTargetResource* DepthRenderResource = DepthImgCaptureComp->TextureTarget->GameThread_GetRenderTargetResource();	
	ReadPixels(DepthRenderResource, DepthImage);
	DepthPixelFence.BeginFence();
}

void AUVisionlogger::ReadPixels(FSceneViewport *& viewport, TArray<FColor>& OutImageData, FReadSurfaceDataFlags InFlags, FIntRect InRect)
{
	
	if (InRect == FIntRect(0, 0, 0, 0))
	{
		InRect = FIntRect(0, 0, viewport->GetSizeXY().X, viewport->GetSizeXY().Y);
	}

	// Read the render target surface data back.	
	struct FReadSurfaceContext
	{
		FRenderTarget* SrcRenderTarget;
		TArray<FColor>* OutData;
		FIntRect Rect;
		FReadSurfaceDataFlags Flags;
	};

	OutImageData.Reset();
	FReadSurfaceContext ReadSurfaceContext =
	{
		viewport,
		&OutImageData,
		InRect,
		InFlags
	};

	ENQUEUE_UNIQUE_RENDER_COMMAND_ONEPARAMETER(
		ReadSurfaceCommand,
		FReadSurfaceContext, Context, ReadSurfaceContext,
		{
			RHICmdList.ReadSurfaceData(
				Context.SrcRenderTarget->GetRenderTargetTexture(),
				Context.Rect,
				*Context.OutData,
				Context.Flags
			);
		});
}

void AUVisionlogger::ReadPixels(FTextureRenderTargetResource *& RenderResource, TArray<FColor>& OutImageData, FReadSurfaceDataFlags InFlags, FIntRect InRect)
{
	
	// Read the render target surface data back.	
	if (InRect == FIntRect(0, 0, 0, 0))
	{
		InRect = FIntRect(0, 0, RenderResource->GetSizeXY().X, RenderResource->GetSizeXY().Y);
	}
	struct FReadSurfaceContext
	{
		FRenderTarget* SrcRenderTarget;
		TArray<FColor>* OutData;
		FIntRect Rect;
		FReadSurfaceDataFlags Flags;
	};

	OutImageData.Reset();
	FReadSurfaceContext ReadSurfaceContext =
	{
		RenderResource,
		&OutImageData,
		InRect,
		InFlags,
	};

	ENQUEUE_UNIQUE_RENDER_COMMAND_ONEPARAMETER(
		ReadSurfaceCommand,
		FReadSurfaceContext, Context, ReadSurfaceContext,
		{
			RHICmdList.ReadSurfaceData(
				Context.SrcRenderTarget->GetRenderTargetTexture(),
				Context.Rect,
				*Context.OutData,
				Context.Flags
			);
		});
}

bool AUVisionlogger::ColorAllObjects()
{
	uint32_t NumberOfActors = 0;

	for (TActorIterator<AActor> ActItr(GetWorld()); ActItr; ++ActItr)
	{

		FString ActorName = ActItr->GetName();
		//GEditor->SelectActor(*ActItr, true, true);
		FString CategoryName = ActorName.Left(7);
		if (!ObjectCategory.Contains(CategoryName)) {
			++NumberOfActors;
			ObjectCategory.Add(CategoryName);
		}
		
	}

	UE_LOG(LogTemp, Warning, TEXT("Found %d Actor Categories."), NumberOfActors);
	GenerateColors(NumberOfActors);
	for (TActorIterator<AActor> ActItr(GetWorld()); ActItr; ++ActItr)
	{
		FString ActorName = ActItr->GetName();
		FString CategoryName = ActorName.Left(7);
		if (!ObjectToColor.Contains(CategoryName))
		{
			check(ColorsUsed < (uint32)ObjectColors.Num());
			ObjectToColor.Add(CategoryName, ColorsUsed);
			UE_LOG(LogTemp, Warning, TEXT("Adding color %d for object %s."), ColorsUsed, *CategoryName);

			++ColorsUsed;
		}

		ColorObject(*ActItr, CategoryName);
	}
	return true;
}

bool AUVisionlogger::ColorObject(AActor * Actor, const FString & name)
{
	const FColor &ObjectColor = ObjectColors[ObjectToColor[name]];
	TArray<UMeshComponent *> PaintableComponents;
	Actor->GetComponents<UMeshComponent>(PaintableComponents);
	for (auto MeshComponent : PaintableComponents)

	{
		if (MeshComponent == nullptr)
			continue;

		if (UStaticMeshComponent *StaticMeshComponent = Cast<UStaticMeshComponent>(MeshComponent))
		{
			if (UStaticMesh *StaticMesh = StaticMeshComponent->GetStaticMesh())
			{
				uint32 PaintingMeshLODIndex = 0;
				uint32 NumLODLevel = StaticMesh->RenderData->LODResources.Num();
				//check(NumLODLevel == 1);
				FStaticMeshLODResources &LODModel = StaticMesh->RenderData->LODResources[PaintingMeshLODIndex];
				StaticMeshComponent->SetLODDataCount(1, StaticMeshComponent->LODData.Num());
				FStaticMeshComponentLODInfo *InstanceMeshLODInfo = &StaticMeshComponent->LODData[PaintingMeshLODIndex];

				// PaintingMeshLODIndex + 1 is the minimum requirement, enlarge if not satisfied

				InstanceMeshLODInfo->PaintedVertices.Empty();

				InstanceMeshLODInfo->OverrideVertexColors = new FColorVertexBuffer;

				InstanceMeshLODInfo->OverrideVertexColors->InitFromSingleColor(FColor::Green, LODModel.GetNumVertices());


				uint32 NumVertices = LODModel.GetNumVertices();
				//check(InstanceMeshLODInfo->OverrideVertexColors);
				//check(NumVertices <= InstanceMeshLODInfo->OverrideVertexColors->GetNumVertices());


				for (uint32 ColorIndex = 0; ColorIndex < NumVertices; ColorIndex++)
				{
					//uint32 NumOverrideVertexColors = InstanceMeshLODInfo->OverrideVertexColors->GetNumVertices();
					//uint32 NumPaintedVertices = InstanceMeshLODInfo->PaintedVertices.Num();
					InstanceMeshLODInfo->OverrideVertexColors->VertexColor(ColorIndex) = ObjectColor;
				}
				BeginInitResource(InstanceMeshLODInfo->OverrideVertexColors);

				StaticMeshComponent->MarkRenderStateDirty();

				//UE_LOG(LogTemp, Warning, TEXT("%s:%s has %d vertices,%d,%d,%d"), *Actor->GetActorLabel(), *StaticMeshComponent->GetName(), NumVertices, InstanceMeshLODInfo->OverrideVertexColors->VertexColor(0).R, InstanceMeshLODInfo->OverrideVertexColors->VertexColor(0).G, InstanceMeshLODInfo->OverrideVertexColors->VertexColor(0).B)
			}
		}
	}
	return true;
}

void AUVisionlogger::GenerateColors(const uint32_t NumberOfColors)
{
	const int32_t MaxHue = 50;
	// It shifts the next Hue value used, so that colors next to each other are not very similar. This is just important for humans
	const int32_t ShiftHue = 11;
	const float MinSat = 0.65;
	const float MinVal = 0.65;

	uint32_t HueCount = MaxHue;
	uint32_t SatCount = 1;
	uint32_t ValCount = 1;

	// Compute how many different Saturations and Values are needed
	int32_t left = std::max<int32_t>(0, NumberOfColors - HueCount);
	while (left > 0)
	{
		if (left > 0)
		{
			++ValCount;
			left = NumberOfColors - SatCount * ValCount * HueCount;
		}
		if (left > 0)
		{
			++SatCount;
			left = NumberOfColors - SatCount * ValCount * HueCount;
		}
	}

	const float StepHue = 360.0f / HueCount;
	const float StepSat = (1.0f - MinSat) / std::max(1.0f, SatCount - 1.0f);
	const float StepVal = (1.0f - MinVal) / std::max(1.0f, ValCount - 1.0f);

	ObjectColors.Reserve(SatCount * ValCount * HueCount);
	UE_LOG(LogTemp, Warning, TEXT("Generating %d colors."), SatCount * ValCount * HueCount);

	FLinearColor HSVColor;
	for (uint32_t s = 0; s < SatCount; ++s)
	{
		HSVColor.G = 1.0f - s * StepSat;
		for (uint32_t v = 0; v < ValCount; ++v)
		{
			HSVColor.B = 1.0f - v * StepVal;
			for (uint32_t h = 0; h < HueCount; ++h)
			{
				HSVColor.R = ((h * ShiftHue) % MaxHue) * StepHue;
				ObjectColors.Add(HSVColor.HSVToLinearRGB().ToFColor(false));
				UE_LOG(LogTemp, Warning, TEXT("Added color %d: %d %d %d"), ObjectColors.Num(), ObjectColors.Last().R, ObjectColors.Last().G, ObjectColors.Last().B);
			}
		}
	}
}

void AUVisionlogger::ShowFlagsBasicSetting(FEngineShowFlags & ShowFlags) const
{
	ShowFlags = FEngineShowFlags(EShowFlagInitMode::ESFIM_All0);
	ShowFlags.SetRendering(true);
	ShowFlags.SetStaticMeshes(true);
	ShowFlags.SetMaterials(true);
	/*ShowFlags.SetInstancedFoliage(true);
	ShowFlags.SetInstancedGrass(true);
	ShowFlags.SetInstancedStaticMeshes(true);
	ShowFlags.SetSkeletalMeshes(true);*/
}

void AUVisionlogger::ShowFlagsLit(FEngineShowFlags & ShowFlags) const
{
	ShowFlagsBasicSetting(ShowFlags);
	ShowFlags = FEngineShowFlags(EShowFlagInitMode::ESFIM_Game);
	ApplyViewMode(VMI_Lit, true, ShowFlags);
	ShowFlags.SetMaterials(true);
	ShowFlags.SetLighting(true);
	ShowFlags.SetPostProcessing(true);
	// ToneMapper needs to be enabled, otherwise the screen will be very dark
	ShowFlags.SetTonemapper(true);
	// TemporalAA needs to be disabled, otherwise the previous frame might contaminate current frame.
	// Check: https://answers.unrealengine.com/questions/436060/low-quality-screenshot-after-setting-the-actor-pos.html for detail
	ShowFlags.SetTemporalAA(false);
	ShowFlags.SetAntiAliasing(true);
	ShowFlags.SetEyeAdaptation(false);// Eye adaption is a slow temporal procedure, not useful for image capture
}

void AUVisionlogger::ShowFlagsPostProcess(FEngineShowFlags & ShowFlags) const
{
	ShowFlagsBasicSetting(ShowFlags);
	ShowFlags.SetPostProcessing(true);
	ShowFlags.SetPostProcessMaterial(true);

	GVertexColorViewMode = EVertexColorViewMode::Color;
}

void AUVisionlogger::ShowFlagsVertexColor(FEngineShowFlags & ShowFlags) const
{
	ShowFlagsLit(ShowFlags);
	ApplyViewMode(VMI_Lit, true, ShowFlags);

	// From MeshPaintEdMode.cpp:2942
	ShowFlags.SetMaterials(false);
	ShowFlags.SetLighting(false);
	ShowFlags.SetBSPTriangles(true);
	ShowFlags.SetVertexColors(true);
	ShowFlags.SetPostProcessing(false);
	ShowFlags.SetHMDDistortion(false);
	ShowFlags.SetTonemapper(false); // This won't take effect here

	GVertexColorViewMode = EVertexColorViewMode::Color;
}
