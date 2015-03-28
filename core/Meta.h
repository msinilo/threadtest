#ifndef CORE_META_H
#define CORE_META_H

namespace rde
{
template<int I>
struct MetaCase
{
	enum
	{
		Result = I
	};
};

template<bool, typename Then, typename Else>
struct MetaIf
{
	typedef Then	Result;
};
template<typename Then, typename Else>
struct MetaIf<false, Then, Else>
{
	typedef Else	Result;
};
template<bool Cond0 = false, bool Cond1 = false, bool Cond2 = false>
struct MetaSwitch
{
	typedef typename MetaIf<Cond0, MetaCase<0>, 
		typename MetaIf<Cond1, MetaCase<1>, 
			typename MetaIf<Cond2, MetaCase<2>, MetaCase<-1> >::Result>::Result>::Result	Result;
};

}

#endif
